#include "database.hpp"
#include "face_detector.hpp"
#include "face_recognizer.hpp"
#include "face_tracker.hpp"
#include "types.hpp"

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

    // Auto-resolve model_dir and db_path when launched from repo root or cpp/build
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

static cv::VideoCapture openCamera(const fr::AppConfig& cfg) {
    cv::VideoCapture cap;
#ifdef _WIN32
    cap.open(cfg.camera_id, cv::CAP_DSHOW);
    if (!cap.isOpened()) cap.open(cfg.camera_id);
#else
    cap.open(cfg.camera_id, cv::CAP_V4L2);
    if (!cap.isOpened()) cap.open(cfg.camera_id);
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
    std::cout << "  Lightweight Native C++ Face Recognition (Edge AI Optimized)\n";
    std::cout << "  Target: Raspberry Pi 3 / Pi 5 / SBC / Windows (Zero-Copy Native)\n";
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
    cv::VideoCapture cap = openCamera(cfg);
    if (!cap.isOpened()) {
        std::cerr << "[FATAL] Could not open camera ID " << cfg.camera_id << "\n";
        return 1;
    }

    const int actual_w = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    const int actual_h = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    std::cout << "[OK] Camera opened: " << actual_w << "x" << actual_h
              << " | Detect Width: " << cfg.detect_width
              << "px | Threshold: " << cfg.threshold
              << (cfg.pi3_mode ? " [Pi 3 Mode]" : "") << "\n";
    if (!cfg.headless) {
        std::cout << "[i] Controls: [q / ESC] Quit | [r] Reload DB | [f] Toggle Flip\n";
    }

    // Shared State for Decoupled Multi-Threaded Pipeline
    std::atomic<bool> running{true};

    // Camera Thread -> Main Thread
    std::mutex cam_mtx;
    cv::Mat latest_cam_frame;
    uint64_t cam_seq = 0;
    double cam_fps_smooth = 0.0;

    // Main Thread -> AI Worker Thread
    std::mutex ai_in_mtx;
    std::condition_variable ai_in_cv;
    cv::Mat ai_pending_frame;
    bool ai_has_frame = false;

    // AI Worker Thread -> Main Thread
    std::mutex ai_out_mtx;
    std::vector<fr::TrackedFace> ai_latest_faces;
    uint64_t ai_result_seq = 0;
    double ai_total_ms = 0.0;

    // 3. Dedicated Camera Capture Thread (Zero I/O Blocking)
    std::thread cam_thread([&]() {
        auto prev_t = Clock::now();
        while (running.load()) {
            cv::Mat frame;
            if (cap.read(frame) && !frame.empty()) {
                if (cfg.flip_horizontal) {
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
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }
    });

    // 4. Dedicated AI Inference Worker Thread (YuNet + MobileFaceNet INT8 + SQLite)
    std::thread ai_thread([&]() {
        while (running.load()) {
            cv::Mat work_frame;
            {
                std::unique_lock<std::mutex> lock(ai_in_mtx);
                ai_in_cv.wait_for(lock, std::chrono::milliseconds(20), [&]() {
                    return ai_has_frame || !running.load();
                });
                if (!running.load()) break;
                if (!ai_has_frame) continue;
                work_frame = std::move(ai_pending_frame);
                ai_has_frame = false;
            }

            const auto t0 = Clock::now();
            auto dets = detector.detect(work_frame, cfg.detect_width);
            const auto t1 = Clock::now();
            const double detect_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

            std::vector<fr::TrackedFace> results;
            results.reserve(dets.size());
            double sum_embed_ms = 0.0;
            double sum_match_ms = 0.0;

            for (const auto& det : dets) {
                const auto te0 = Clock::now();
                cv::Mat aligned;
                if (det.has_landmarks) {
                    aligned = fr::FaceDetector::alignFace5Pts(work_frame, det.landmarks, 112);
                } else {
                    cv::resize(work_frame(det.box), aligned, cv::Size(112, 112));
                }
                auto emb = recognizer.extractEmbedding(aligned);
                const auto te1 = Clock::now();

                const auto tm0 = Clock::now();
                auto match = db.findMatch(emb, cfg.threshold);
                const auto tm1 = Clock::now();

                const double embed_ms = std::chrono::duration<double, std::milli>(te1 - te0).count();
                const double match_ms = std::chrono::duration<double, std::milli>(tm1 - tm0).count();
                sum_embed_ms += embed_ms;
                sum_match_ms += match_ms;

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

            {
                std::lock_guard<std::mutex> lock(ai_out_mtx);
                ai_latest_faces = std::move(results);
                ai_result_seq++;
                ai_total_ms = detect_ms + sum_embed_ms + sum_match_ms;
            }
        }
    });

    // 5. Main Native Display & Zero-Lag Optical Flow Loop (Full Camera FPS)
    fr::OpticalFlowFaceTracker tracker(0.55);
    uint64_t last_cam_seq = 0;
    uint64_t last_ai_seq = 0;
    double disp_fps_smooth = 0.0;
    auto prev_disp_t = Clock::now();
    std::unordered_map<std::string, Clock::time_point> last_log_time;

    const std::string win_name = "Lightweight Native C++ Face Recognition (" + recognizer.backendName() + ")";
    if (!cfg.headless) {
        cv::namedWindow(win_name, cv::WINDOW_NORMAL);
    }

    while (running.load()) {
        cv::Mat frame;
        double cur_cam_fps = 0.0;
        uint64_t cur_seq = 0;
        {
            std::lock_guard<std::mutex> lock(cam_mtx);
            if (cam_seq == last_cam_seq || latest_cam_frame.empty()) {
                // Wait briefly for next hardware frame
            } else {
                frame = latest_cam_frame.clone();
                cur_cam_fps = cam_fps_smooth;
                cur_seq = cam_seq;
            }
        }

        if (frame.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        last_cam_seq = cur_seq;

        // Push latest frame to AI worker (non-blocking)
        {
            std::lock_guard<std::mutex> lock(ai_in_mtx);
            ai_pending_frame = frame.clone();
            ai_has_frame = true;
        }
        ai_in_cv.notify_one();

        // Fetch latest AI results
        std::vector<fr::TrackedFace> ai_faces;
        bool has_new_ai = false;
        double cur_ai_ms = 0.0;
        {
            std::lock_guard<std::mutex> lock(ai_out_mtx);
            cur_ai_ms = ai_total_ms;
            if (ai_result_seq != last_ai_seq) {
                ai_faces = ai_latest_faces;
                last_ai_seq = ai_result_seq;
                has_new_ai = true;
            }
        }

        // Update Zero-Lag Optical Flow Tracker on EVERY camera frame (< 1.5 ms)
        auto active_faces = tracker.updateFrame(frame, ai_faces, has_new_ai);

        // Compute Display FPS
        const auto now = Clock::now();
        const double dt = std::max(1e-5, std::chrono::duration<double>(now - prev_disp_t).count());
        prev_disp_t = now;
        const double inst_disp_fps = 1.0 / dt;
        disp_fps_smooth = (disp_fps_smooth > 0.0)
                              ? (0.85 * disp_fps_smooth + 0.15 * inst_disp_fps)
                              : inst_disp_fps;

        // Draw Bounding Boxes, 5 Landmarks & Debounced SQLite Logging
        for (const auto& f : active_faces) {
            const bool is_known = (f.match.status == "KNOWN");
            const cv::Scalar box_color = is_known ? cv::Scalar(0, 230, 0) : cv::Scalar(0, 165, 255);
            const cv::Scalar bg_color = is_known ? cv::Scalar(0, 150, 0) : cv::Scalar(0, 110, 190);

            cv::rectangle(frame, f.box, box_color, 2);

            if (f.has_landmarks) {
                for (const auto& lm : f.landmarks) {
                    cv::circle(frame, lm, 2, cv::Scalar(0, 255, 255), -1);
                }
            }

            std::ostringstream lbl;
            lbl << f.match.name << " (" << std::fixed << std::setprecision(2) << f.match.similarity << ")";
            const std::string label_str = lbl.str();

            const int banner_h = 24;
            const int bg_w = std::max(140, static_cast<int>(label_str.size()) * 9);
            const int y_top = std::max(0, f.box.y - banner_h);
            cv::rectangle(frame, cv::Rect(f.box.x, y_top, bg_w, banner_h), bg_color, -1);
            cv::putText(frame, label_str, cv::Point(f.box.x + 4, y_top + 17),
                        cv::FONT_HERSHEY_SIMPLEX, 0.48, cv::Scalar(255, 255, 255), 1, cv::LINE_AA);

            // Debounced DB logging (once every 5 seconds per person)
            auto it = last_log_time.find(f.match.name);
            if (it == last_log_time.end() ||
                std::chrono::duration<double>(now - it->second).count() > 5.0) {
                db.logRecognition(f.match, cfg.threshold, f.detect_ms, f.embed_ms, f.match_ms);
                last_log_time[f.match.name] = now;
            }
        }

        // Top HUD Bar
        cv::rectangle(frame, cv::Rect(0, 0, frame.cols, 28), cv::Scalar(18, 18, 18), -1);
        cv::line(frame, cv::Point(0, 28), cv::Point(frame.cols, 28), cv::Scalar(0, 180, 255), 1);
        std::ostringstream hud;
        hud << "C++ Native | Cam:" << std::fixed << std::setprecision(1) << cur_cam_fps
            << " | Disp:" << disp_fps_smooth
            << " FPS | AI:" << cur_ai_ms
            << "ms | DB:" << db.enrolledPersonCount()
            << (recognizer.isInt8() ? " | INT8" : " | FP32");
        cv::putText(frame, hud.str(), cv::Point(8, 19),
                    cv::FONT_HERSHEY_SIMPLEX, 0.42, cv::Scalar(0, 255, 255), 1, cv::LINE_AA);

        if (!cfg.headless) {
            cv::imshow(win_name, frame);
            const int key = cv::waitKey(1) & 0xFF;
            if (key == 'q' || key == 27) {
                running.store(false);
            } else if (key == 'r') {
                db.reloadCache();
            } else if (key == 'f') {
                cfg.flip_horizontal = !cfg.flip_horizontal;
            }
        }
    }

    // Graceful Shutdown
    running.store(false);
    ai_in_cv.notify_all();
    if (cam_thread.joinable()) cam_thread.join();
    if (ai_thread.joinable()) ai_thread.join();
    cap.release();
    if (!cfg.headless) cv::destroyAllWindows();

    std::cout << "[OK] Native C++ Face Recognition exited cleanly.\n";
    return 0;
}
