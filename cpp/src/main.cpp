#include "database.hpp"
#include "face_detector.hpp"
#include "face_recognizer.hpp"
#include "face_tracker.hpp"
#include "types.hpp"
#include "ui_dashboard.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

static void printUsage(const char* prog) {
    std::cout
        << "Usage: " << prog << " [options]\n"
        << "Options:\n"
        << "  --pi3                  Preset for Raspberry Pi 3 (320x240, detect-width 256)\n"
        << "  --cam <id>             Camera device ID (default: 0)\n"
        << "  --width <px>           Camera capture width (default: 640)\n"
        << "  --height <px>          Camera capture height (default: 480)\n"
        << "  --fps <n>              Target camera FPS (default: 60)\n"
        << "  --detect-width <px>    Downscale width for YuNet detector (default: 400)\n"
        << "  --threshold <f>        Cosine similarity threshold (default: 0.60)\n"
        << "  --model-dir <path>     Directory containing ONNX models (default: ../Antigravity)\n"
        << "  --db <path>            Path to SQLite database (default: ../Antigravity/face_recognition.db)\n"
        << "  --headless             Run without GUI window (console logging only)\n"
        << "  --no-flip              Disable mirror horizontal flip\n"
        << "  -h, --help             Show this help message\n";
}

static fr::AppConfig parseArgs(int argc, char** argv) {
    fr::AppConfig cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto nextVal = [&](const std::string& flag) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "[ERROR] Missing value for " << flag << "\n";
                std::exit(1);
            }
            return argv[++i];
        };

        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            std::exit(0);
        } else if (arg == "--pi3") {
            cfg.pi3_mode = true;
            cfg.width = 320;
            cfg.height = 240;
            cfg.target_fps = 30;
            cfg.detect_width = 256;
        } else if (arg == "--cam") {
            cfg.camera_id = std::stoi(nextVal(arg));
        } else if (arg == "--width") {
            cfg.width = std::stoi(nextVal(arg));
        } else if (arg == "--height") {
            cfg.height = std::stoi(nextVal(arg));
        } else if (arg == "--fps") {
            cfg.target_fps = std::stoi(nextVal(arg));
        } else if (arg == "--detect-width") {
            cfg.detect_width = std::stoi(nextVal(arg));
        } else if (arg == "--threshold") {
            cfg.threshold = std::stof(nextVal(arg));
        } else if (arg == "--model-dir") {
            cfg.model_dir = nextVal(arg);
        } else if (arg == "--db") {
            cfg.db_path = nextVal(arg);
        } else if (arg == "--headless") {
            cfg.headless = true;
        } else if (arg == "--no-flip") {
            cfg.flip_horizontal = false;
        }
    }

    if (!fs::exists(fs::path(cfg.model_dir) / "face_detection_yunet_2023mar.onnx")) {
        for (const auto& probe : {"Antigravity", "../Antigravity", "../../Antigravity", "."}) {
            if (fs::exists(fs::path(probe) / "face_detection_yunet_2023mar.onnx")) {
                cfg.model_dir = probe;
                if (!fs::exists(cfg.db_path)) {
                    cfg.db_path = (fs::path(probe) / "face_recognition.db").string();
                }
                break;
            }
        }
    }
    return cfg;
}

static cv::VideoCapture openCamera(const fr::AppConfig& cfg, int cam_id) {
    cv::VideoCapture cap;
#ifdef _WIN32
    cap.open(cam_id, cv::CAP_DSHOW);
    if (!cap.isOpened()) cap.open(cam_id);
#else
    cap.open(cam_id, cv::CAP_V4L2);
    if (!cap.isOpened()) cap.open(cam_id);
#endif
    if (cap.isOpened()) {
        cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
        cap.set(cv::CAP_PROP_FRAME_WIDTH, cfg.width);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, cfg.height);
        cap.set(cv::CAP_PROP_FPS, cfg.target_fps);
        cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
    }
    return cap;
}

int main(int argc, char** argv) {
    fr::AppConfig cfg = parseArgs(argc, argv);

    std::cout << "====================================================================\n";
    std::cout << "  Lightweight Native C++ Face Recognition + ImGui Dashboard\n";
    std::cout << "  Target: Raspberry Pi 3 / Pi 5 / SBC / Windows (Zero-Copy OpenGL)\n";
    std::cout << "====================================================================\n";

    // 1. Initialize Detector, Recognizer, Database
    fr::FaceDetector detector;
    const std::string yunet_path = (fs::path(cfg.model_dir) / "face_detection_yunet_2023mar.onnx").string();
    if (!detector.init(yunet_path)) {
        std::cerr << "[FATAL] YuNet model not found at: " << yunet_path << "\n";
        return 1;
    }

    fr::FaceRecognizer recognizer;
    const int threads = std::max(1u, std::thread::hardware_concurrency());
    if (!recognizer.init(cfg.model_dir, threads)) {
        std::cerr << "[FATAL] MobileFaceNet model could not be loaded from: " << cfg.model_dir << "\n";
        return 1;
    }

    fr::FaceDatabase db;
    if (!db.open(cfg.db_path)) {
        std::cerr << "[FATAL] Could not open database: " << cfg.db_path << "\n";
        return 1;
    }
    db.reloadCache();

    // 2. Open Camera
    std::mutex cam_hw_mtx;
    cv::VideoCapture cap = openCamera(cfg, cfg.camera_id);
    if (!cap.isOpened()) {
        std::cerr << "[WARN] Could not open camera ID " << cfg.camera_id << " at startup. You can switch camera in Settings.\n";
    } else {
        std::cout << "[OK] Camera opened: "
                  << static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH)) << "x"
                  << static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT))
                  << " | Detect Width: " << cfg.detect_width << "px"
                  << (cfg.pi3_mode ? " [Pi 3 Mode]" : "") << "\n";
    }

    // 3. Initialize Native Dashboard UI (Dear ImGui + OpenGL)
    fr::NativeDashboardUI ui;
    bool ui_active = false;
    if (!cfg.headless) {
        const std::string title = "Edge Face Recognition — C++17 Native Dashboard (" + recognizer.backendName() + ")";
        ui_active = ui.init(title, cfg.pi3_mode ? 960 : 1240, cfg.pi3_mode ? 600 : 740);
        if (!ui_active) {
            std::cerr << "[WARN] Native GUI could not open. Switching to headless mode.\n";
            cfg.headless = true;
        }
    }

    // Shared State for Decoupled Multi-Threaded Pipeline
    std::atomic<bool> running{true};
    std::atomic<float> live_threshold{cfg.threshold};
    std::atomic<int> live_detect_width{cfg.detect_width};
    std::atomic<int> live_detect_every_n{cfg.detect_every_n_frames};
    std::atomic<float> live_rec_interval_sec{cfg.rec_interval_sec};
    std::atomic<bool> live_flip{cfg.flip_horizontal};

    // Mutex protecting AI models during live enrollment
    std::mutex ai_engine_mtx;

    // Camera Thread -> Main Thread
    std::mutex cam_mtx;
    cv::Mat latest_cam_frame;
    uint64_t cam_seq = 0;
    double cam_fps_smooth = 0.0;

    // Main Thread -> AI Worker Thread
    std::mutex ai_in_mtx;
    std::condition_variable ai_in_cv;
    cv::Mat ai_pending_frame;
    uint64_t ai_pending_seq = 0;
    bool ai_has_frame = false;

    // AI Worker Thread -> Main Thread
    std::mutex ai_out_mtx;
    std::vector<fr::TrackedFace> ai_latest_faces;
    uint64_t ai_result_seq = 0;
    uint64_t ai_result_frame_seq = 0;
    double ai_total_ms = 0.0;

    // 4. Dedicated Camera Capture Thread (Zero I/O Blocking)
    std::thread cam_thread([&]() {
        auto prev_t = Clock::now();
        while (running.load()) {
            cv::Mat frame;
            bool ok = false;
            {
                std::lock_guard<std::mutex> hw_lock(cam_hw_mtx);
                if (cap.isOpened()) {
                    ok = cap.read(frame) && !frame.empty();
                }
            }
            if (ok) {
                if (live_flip.load()) {
                    cv::flip(frame, frame, 1);
                }
                const auto now = Clock::now();
                const double dt = std::max(1e-5, std::chrono::duration<double>(now - prev_t).count());
                prev_t = now;
                const double inst_fps = 1.0 / dt;

                std::lock_guard<std::mutex> lock(cam_mtx);
                latest_cam_frame = std::move(frame);
                cam_seq++;
                cam_fps_smooth = (cam_fps_smooth > 0.0)
                                     ? (0.85 * cam_fps_smooth + 0.15 * inst_fps)
                                     : inst_fps;
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
    });

    // 5. Dedicated AI Inference Worker Thread (YuNet + Cached MobileFaceNet INT8 + SQLite)
    std::thread ai_thread([&]() {
        struct RecCacheEntry {
            cv::Rect box;
            fr::MatchResult match;
            Clock::time_point last_rec;
            Clock::time_point last_seen;
        };
        std::vector<RecCacheEntry> rec_cache;

        auto rectIoU = [](const cv::Rect& a, const cv::Rect& b) -> float {
            const int x1 = std::max(a.x, b.x);
            const int y1 = std::max(a.y, b.y);
            const int x2 = std::min(a.x + a.width, b.x + b.width);
            const int y2 = std::min(a.y + a.height, b.y + b.height);
            const int inter = std::max(0, x2 - x1) * std::max(0, y2 - y1);
            const int uni = std::max(1, a.area() + b.area() - inter);
            return static_cast<float>(inter) / static_cast<float>(uni);
        };

        while (running.load()) {
            cv::Mat work_frame;
            uint64_t work_seq = 0;
            {
                std::unique_lock<std::mutex> lock(ai_in_mtx);
                ai_in_cv.wait_for(lock, std::chrono::milliseconds(15), [&]() {
                    return ai_has_frame || !running.load();
                });
                if (!running.load()) break;
                if (!ai_has_frame) continue;
                work_frame = std::move(ai_pending_frame);
                work_seq = ai_pending_seq;
                ai_has_frame = false;
            }

            const float cur_thr = live_threshold.load();
            const int cur_det_w = live_detect_width.load();
            const auto now_ai = Clock::now();

            std::vector<fr::FaceDetection> dets;
            double detect_ms = 0.0;
            {
                std::lock_guard<std::mutex> eng_lock(ai_engine_mtx);
                const auto t0 = Clock::now();
                dets = detector.detect(work_frame, cur_det_w);
                const auto t1 = Clock::now();
                detect_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            }

            std::vector<fr::TrackedFace> results;
            results.reserve(dets.size());
            double sum_embed_ms = 0.0;
            double sum_match_ms = 0.0;

            for (const auto& det : dets) {
                int cache_idx = -1;
                float best_iou = 0.0f;
                for (size_t c = 0; c < rec_cache.size(); ++c) {
                    const float iou = rectIoU(det.box, rec_cache[c].box);
                    if (iou > best_iou) {
                        best_iou = iou;
                        cache_idx = static_cast<int>(c);
                    }
                }

                fr::MatchResult match;
                double embed_ms = 0.0;
                double match_ms = 0.0;

                const float cur_rec_interval = live_rec_interval_sec.load();
                const bool can_reuse = (cache_idx >= 0 && best_iou >= 0.25f &&
                                        rec_cache[cache_idx].match.status == "KNOWN" &&
                                        std::chrono::duration<double>(now_ai - rec_cache[cache_idx].last_rec).count() < cur_rec_interval);

                if (can_reuse) {
                    match = rec_cache[cache_idx].match;
                    rec_cache[cache_idx].box = det.box;
                    rec_cache[cache_idx].last_seen = now_ai;
                } else {
                    std::vector<float> emb;
                    {
                        std::lock_guard<std::mutex> eng_lock(ai_engine_mtx);
                        const auto te0 = Clock::now();
                        cv::Mat aligned;
                        if (det.has_landmarks) {
                            aligned = fr::FaceDetector::alignFace5Pts(work_frame, det.landmarks, 112);
                        } else {
                            cv::resize(work_frame(det.box), aligned, cv::Size(112, 112));
                        }
                        emb = recognizer.extractEmbedding(aligned);
                        const auto te1 = Clock::now();
                        embed_ms = std::chrono::duration<double, std::milli>(te1 - te0).count();
                    }

                    const auto tm0 = Clock::now();
                    match = db.findMatch(emb, cur_thr);
                    const auto tm1 = Clock::now();
                    match_ms = std::chrono::duration<double, std::milli>(tm1 - tm0).count();
                    sum_embed_ms += embed_ms;
                    sum_match_ms += match_ms;

                    if (cache_idx >= 0 && best_iou >= 0.25f) {
                        rec_cache[cache_idx].box = det.box;
                        rec_cache[cache_idx].match = match;
                        rec_cache[cache_idx].last_rec = now_ai;
                        rec_cache[cache_idx].last_seen = now_ai;
                    } else {
                        rec_cache.push_back({det.box, match, now_ai, now_ai});
                    }
                }

                fr::TrackedFace tf;
                tf.box = det.box;
                tf.landmarks = det.landmarks;
                tf.has_landmarks = det.has_landmarks;
                tf.confidence = det.confidence;
                tf.match = std::move(match);
                tf.detect_ms = detect_ms;
                tf.embed_ms = embed_ms;
                tf.match_ms = match_ms;
                results.push_back(std::move(tf));
            }

            rec_cache.erase(
                std::remove_if(rec_cache.begin(), rec_cache.end(), [&](const RecCacheEntry& e) {
                    return std::chrono::duration<double>(now_ai - e.last_seen).count() > 2.5;
                }),
                rec_cache.end()
            );

            {
                std::lock_guard<std::mutex> lock(ai_out_mtx);
                ai_latest_faces = std::move(results);
                ai_result_seq++;
                ai_result_frame_seq = work_seq;
                ai_total_ms = detect_ms + sum_embed_ms + sum_match_ms;
            }
        }
    });

    // 6. Wire Dashboard Callbacks (Live Enrollment & Camera Switching)
    fr::DashboardCallbacks callbacks;

    callbacks.on_enroll = [&](const std::string& name,
                              const std::string& student_id,
                              const std::string& role,
                              std::string& out_msg) -> bool {
        cv::Mat raw_snap;
        {
            std::lock_guard<std::mutex> lock(cam_mtx);
            if (latest_cam_frame.empty()) {
                out_msg = "[ERROR] No camera frame available.";
                return false;
            }
            raw_snap = latest_cam_frame.clone();
        }

        std::vector<fr::FaceDetection> dets;
        std::vector<float> emb;
        float conf = 1.0f;
        {
            std::lock_guard<std::mutex> eng_lock(ai_engine_mtx);
            dets = detector.detect(raw_snap, std::max(400, live_detect_width.load()));
            if (dets.empty()) {
                out_msg = "[ERROR] No face detected. Please face the camera clearly.";
                return false;
            }
            // Pick largest face in frame
            const auto& best_det = *std::max_element(dets.begin(), dets.end(),
                [](const fr::FaceDetection& a, const fr::FaceDetection& b) {
                    return a.box.area() < b.box.area();
                });
            conf = best_det.confidence;
            cv::Mat aligned = best_det.has_landmarks
                                  ? fr::FaceDetector::alignFace5Pts(raw_snap, best_det.landmarks, 112)
                                  : [&]() {
                                        cv::Mat r;
                                        cv::resize(raw_snap(best_det.box), r, cv::Size(112, 112));
                                        return r;
                                    }();
            emb = recognizer.extractEmbedding(aligned);
        }

        if (emb.empty()) {
            out_msg = "[ERROR] Failed to extract face embedding.";
            return false;
        }
        if (db.enrollFace(name, student_id, role, emb, conf)) {
            out_msg = "[OK] Enrolled '" + name + "' successfully! (Total vectors: " +
                      std::to_string(db.embeddingCount()) + ")";
            return true;
        }
        out_msg = "[ERROR] Database insert failed.";
        return false;
    };

    callbacks.on_switch_camera = [&](int new_cam_id, std::string& out_msg) -> bool {
        cv::VideoCapture new_cap = openCamera(cfg, new_cam_id);
        if (!new_cap.isOpened()) {
            out_msg = "[ERROR] Camera ID " + std::to_string(new_cam_id) + " not available.";
            return false;
        }
        {
            std::lock_guard<std::mutex> hw_lock(cam_hw_mtx);
            cap.release();
            cap = std::move(new_cap);
            cfg.camera_id = new_cam_id;
        }
        out_msg = "[OK] Switched to Camera ID " + std::to_string(new_cam_id);
        return true;
    };

    callbacks.on_change_resolution = [&](int new_w, int new_h, std::string& out_msg) -> bool {
        fr::AppConfig next_cfg = cfg;
        next_cfg.width = new_w;
        next_cfg.height = new_h;

        int actual_w = new_w;
        int actual_h = new_h;
        {
            std::lock_guard<std::mutex> hw_lock(cam_hw_mtx);
            // Reopen camera with requested resolution (most reliable for Linux V4L2 & Windows DShow)
            cap.release();
            cv::VideoCapture new_cap = openCamera(next_cfg, cfg.camera_id);
            if (!new_cap.isOpened()) {
                // Fallback to previous resolution if unsupported
                cap = openCamera(cfg, cfg.camera_id);
                out_msg = "[ERROR] Failed to apply " + std::to_string(new_w) + "x" + std::to_string(new_h);
                return false;
            }
            actual_w = static_cast<int>(new_cap.get(cv::CAP_PROP_FRAME_WIDTH));
            actual_h = static_cast<int>(new_cap.get(cv::CAP_PROP_FRAME_HEIGHT));
            cap = std::move(new_cap);
            cfg.width = actual_w > 0 ? actual_w : new_w;
            cfg.height = actual_h > 0 ? actual_h : new_h;
        }
        out_msg = "[OK] Resolution set to " + std::to_string(cfg.width) + "x" + std::to_string(cfg.height);
        return true;
    };

    // 7. Main Zero-Copy Render & Delay-Compensated Optical Flow Loop (60 FPS)
    fr::OpticalFlowFaceTracker tracker(0.60);
    uint64_t last_cam_seq = 0;
    uint64_t last_ai_seq = 0;
    double disp_fps_smooth = 0.0;
    auto prev_disp_t = Clock::now();
    std::unordered_map<std::string, Clock::time_point> last_log_time;

    cv::Mat last_rendered_frame;
    std::vector<fr::TrackedFace> active_faces;
    double cur_cam_fps = 0.0;
    double cur_ai_ms = 0.0;

    while (running.load()) {
        if (ui_active && ui.shouldClose()) {
            running.store(false);
            break;
        }

        // Sync live settings edited via UI sliders
        live_threshold.store(cfg.threshold);
        live_detect_width.store(cfg.detect_width);
        live_detect_every_n.store(std::max(1, cfg.detect_every_n_frames));
        live_rec_interval_sec.store(cfg.rec_interval_sec);
        live_flip.store(cfg.flip_horizontal);

        cv::Mat frame;
        uint64_t cur_seq = 0;
        {
            std::lock_guard<std::mutex> lock(cam_mtx);
            if (cam_seq != last_cam_seq && !latest_cam_frame.empty()) {
                frame = latest_cam_frame.clone();
                cur_cam_fps = cam_fps_smooth;
                cur_seq = cam_seq;
            }
        }

        if (!frame.empty()) {
            last_cam_seq = cur_seq;

            // Push latest frame to AI worker only every N camera frames
            const int det_every = live_detect_every_n.load();
            if (det_every <= 1 || (cur_seq % static_cast<uint64_t>(det_every)) == 0) {
                {
                    std::lock_guard<std::mutex> lock(ai_in_mtx);
                    ai_pending_frame = frame.clone();
                    ai_pending_seq = cur_seq;
                    ai_has_frame = true;
                }
                ai_in_cv.notify_one();
            }

            // Fetch latest AI results (with originating frame_seq for Delay Compensation)
            std::vector<fr::TrackedFace> ai_faces;
            bool has_new_ai = false;
            uint64_t res_frame_seq = cur_seq;
            {
                std::lock_guard<std::mutex> lock(ai_out_mtx);
                cur_ai_ms = ai_total_ms;
                if (ai_result_seq != last_ai_seq) {
                    ai_faces = ai_latest_faces;
                    last_ai_seq = ai_result_seq;
                    res_frame_seq = ai_result_frame_seq;
                    has_new_ai = true;
                }
            }

            // Update Face Tracker (with live Optical Flow Prediction toggle & Hold Duration)
            active_faces = tracker.updateFrame(
                frame, cur_seq, ai_faces, res_frame_seq, has_new_ai,
                cfg.enable_predict, static_cast<double>(cfg.predict_hold_sec)
            );

            const auto now = Clock::now();
            const double dt = std::max(1e-5, std::chrono::duration<double>(now - prev_disp_t).count());
            prev_disp_t = now;
            const double inst_disp_fps = 1.0 / dt;
            disp_fps_smooth = (disp_fps_smooth > 0.0)
                                  ? (0.85 * disp_fps_smooth + 0.15 * inst_disp_fps)
                                  : inst_disp_fps;

            // Draw Minimal Thin Corner Brackets Only (no text overlay on video — names shown in Log/Sidebar)
            for (const auto& f : active_faces) {
                const bool is_known = (f.match.status == "KNOWN");
                const cv::Scalar box_color = is_known ? cv::Scalar(118, 230, 0) : cv::Scalar(0, 180, 255);

                const int x1 = f.box.x;
                const int y1 = f.box.y;
                const int x2 = f.box.x + f.box.width;
                const int y2 = f.box.y + f.box.height;
                const int corner_len = std::clamp(std::min(f.box.width, f.box.height) / 5, 10, 28);
                constexpr int thin = 1;

                // Top-left corner
                cv::line(frame, cv::Point(x1, y1), cv::Point(x1 + corner_len, y1), box_color, thin, cv::LINE_AA);
                cv::line(frame, cv::Point(x1, y1), cv::Point(x1, y1 + corner_len), box_color, thin, cv::LINE_AA);
                // Top-right corner
                cv::line(frame, cv::Point(x2, y1), cv::Point(x2 - corner_len, y1), box_color, thin, cv::LINE_AA);
                cv::line(frame, cv::Point(x2, y1), cv::Point(x2, y1 + corner_len), box_color, thin, cv::LINE_AA);
                // Bottom-left corner
                cv::line(frame, cv::Point(x1, y2), cv::Point(x1 + corner_len, y2), box_color, thin, cv::LINE_AA);
                cv::line(frame, cv::Point(x1, y2), cv::Point(x1, y2 - corner_len), box_color, thin, cv::LINE_AA);
                // Bottom-right corner
                cv::line(frame, cv::Point(x2, y2), cv::Point(x2 - corner_len, y2), box_color, thin, cv::LINE_AA);
                cv::line(frame, cv::Point(x2, y2), cv::Point(x2, y2 - corner_len), box_color, thin, cv::LINE_AA);

                // Debounced DB logging (once every 5 seconds per person)
                auto it = last_log_time.find(f.match.name);
                if (it == last_log_time.end() ||
                    std::chrono::duration<double>(now - it->second).count() > 5.0) {
                    db.logRecognition(f.match, cfg.threshold, f.detect_ms, f.embed_ms, f.match_ms);
                    last_log_time[f.match.name] = now;
                }
            }

            last_rendered_frame = std::move(frame);
        } else if (!ui_active) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        if (ui_active) {
            fr::TelemetrySnapshot tel;
            tel.cam_fps = cur_cam_fps;
            tel.disp_fps = disp_fps_smooth;
            tel.ai_ms = cur_ai_ms;
            tel.frame_w = last_rendered_frame.empty() ? cfg.width : last_rendered_frame.cols;
            tel.frame_h = last_rendered_frame.empty() ? cfg.height : last_rendered_frame.rows;
            tel.is_int8 = recognizer.isInt8();
            tel.backend_name = recognizer.backendName();
            tel.model_name = recognizer.modelName();

            ui.renderFrame(last_rendered_frame, active_faces, tel, cfg, db, callbacks);
        }
    }

    // Graceful Shutdown
    running.store(false);
    ai_in_cv.notify_all();
    if (cam_thread.joinable()) cam_thread.join();
    if (ai_thread.joinable()) ai_thread.join();
    {
        std::lock_guard<std::mutex> hw_lock(cam_hw_mtx);
        cap.release();
    }
    ui.shutdown();

    std::cout << "[OK] Native C++ Face Recognition Dashboard exited cleanly.\n";
    return 0;
}
