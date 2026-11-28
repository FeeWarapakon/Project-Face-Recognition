#include "face_tracker.hpp"
#include <algorithm>
#include <cmath>

namespace fr {

OpticalFlowFaceTracker::OpticalFlowFaceTracker(double decay_sec)
    : decay_sec_(decay_sec) {}

float OpticalFlowFaceTracker::computeIoU(const cv::Rect2f& a, const cv::Rect2f& b) {
    const float x1 = std::max(a.x, b.x);
    const float y1 = std::max(a.y, b.y);
    const float x2 = std::min(a.x + a.width, b.x + b.width);
    const float y2 = std::min(a.y + a.height, b.y + b.height);
    const float inter = std::max(0.0f, x2 - x1) * std::max(0.0f, y2 - y1);
    const float union_area = std::max(1.0f, a.area() + b.area() - inter);
    return inter / union_area;
}

std::vector<cv::Point2f> OpticalFlowFaceTracker::extractFaceFeatures(
    const cv::Mat& gray,
    const cv::Rect2f& box,
    const std::array<cv::Point2f, 5>& landmarks,
    bool has_landmarks) {
    std::vector<cv::Point2f> pts;
    pts.reserve(24);

    // 1. Detect high-contrast Shi-Tomasi corners inside the inner 70% of the face box
    //    (focuses on eyes, eyebrows, nostrils, mouth corners — avoids background pixels!)
    const int margin_x = static_cast<int>(box.width * 0.15f);
    const int margin_y = static_cast<int>(box.height * 0.15f);
    const int rx = std::clamp(static_cast<int>(box.x) + margin_x, 0, std::max(0, gray.cols - 2));
    const int ry = std::clamp(static_cast<int>(box.y) + margin_y, 0, std::max(0, gray.rows - 2));
    const int rw = std::clamp(static_cast<int>(box.width) - 2 * margin_x, 1, gray.cols - rx);
    const int rh = std::clamp(static_cast<int>(box.height) - 2 * margin_y, 1, gray.rows - ry);

    if (rw >= 12 && rh >= 12) {
        cv::Mat roi = gray(cv::Rect(rx, ry, rw, rh));
        std::vector<cv::Point2f> corners;
        cv::goodFeaturesToTrack(roi, corners, 18, 0.02, std::max(4.0, rw * 0.08));
        for (const auto& c : corners) {
            pts.emplace_back(c.x + static_cast<float>(rx), c.y + static_cast<float>(ry));
        }
    }

    // 2. Always include the 5 semantic facial landmarks
    if (has_landmarks) {
        for (const auto& lm : landmarks) {
            if (lm.x >= 0 && lm.x < gray.cols && lm.y >= 0 && lm.y < gray.rows) {
                pts.push_back(lm);
            }
        }
    }

    // 3. Fallback geometric grid if low texture
    if (pts.size() < 5) {
        const float cx = box.x + box.width * 0.5f;
        const float cy = box.y + box.height * 0.5f;
        pts.emplace_back(cx, cy);
        pts.emplace_back(cx, box.y + box.height * 0.30f);
        pts.emplace_back(cx, box.y + box.height * 0.70f);
        pts.emplace_back(box.x + box.width * 0.30f, cy);
        pts.emplace_back(box.x + box.width * 0.70f, cy);
    }

    return pts;
}

static float medianOf(std::vector<float>& vals) {
    const size_t n = vals.size();
    std::nth_element(vals.begin(), vals.begin() + n / 2, vals.end());
    return vals[n / 2];
}

std::vector<TrackedFace> OpticalFlowFaceTracker::updateFrame(
    const cv::Mat& frame_bgr,
    uint64_t current_seq,
    const std::vector<TrackedFace>& ai_faces,
    uint64_t ai_frame_seq,
    bool has_new_detections,
    bool enable_predict,
    double decay_sec_override) {
    const double active_decay = (decay_sec_override > 0.0) ? decay_sec_override : decay_sec_;
    const auto now = std::chrono::steady_clock::now();
    const float frame_w = static_cast<float>(frame_bgr.cols);
    const float frame_h = static_cast<float>(frame_bgr.rows);

    cv::Mat gray;
    if (enable_predict) {
        cv::cvtColor(frame_bgr, gray, cv::COLOR_BGR2GRAY);
    }

    // 1. Forward-Backward Lucas-Kanade Sparse Optical Flow on EVERY camera frame (< 1.5 ms)
    if (enable_predict && !prev_gray_.empty() && prev_gray_.size() == gray.size() && !tracks_.empty()) {
        for (auto& [tid, trk] : tracks_) {
            float dx = 0.0f;
            float dy = 0.0f;

            if (trk.flow_pts.size() >= 3) {
                std::vector<cv::Point2f> next_pts;
                std::vector<uchar> status_fwd;
                std::vector<float> err_fwd;

                cv::calcOpticalFlowPyrLK(
                    prev_gray_, gray, trk.flow_pts, next_pts, status_fwd, err_fwd,
                    cv::Size(19, 19), 3,
                    cv::TermCriteria(cv::TermCriteria::COUNT | cv::TermCriteria::EPS, 12, 0.03)
                );

                // Filter out noisy / outlier points using Forward-Backward consistency check
                std::vector<cv::Point2f> back_pts;
                std::vector<uchar> status_bwd;
                std::vector<float> err_bwd;
                cv::calcOpticalFlowPyrLK(
                    gray, prev_gray_, next_pts, back_pts, status_bwd, err_bwd,
                    cv::Size(19, 19), 3,
                    cv::TermCriteria(cv::TermCriteria::COUNT | cv::TermCriteria::EPS, 12, 0.03)
                );

                std::vector<float> dxs;
                std::vector<float> dys;
                std::vector<cv::Point2f> surviving_pts;
                dxs.reserve(status_fwd.size());
                dys.reserve(status_fwd.size());
                surviving_pts.reserve(status_fwd.size());

                for (size_t i = 0; i < status_fwd.size(); ++i) {
                    if (status_fwd[i] && status_bwd[i]) {
                        const float fb_err = static_cast<float>(cv::norm(trk.flow_pts[i] - back_pts[i]));
                        if (fb_err < 1.8f) {
                            dxs.push_back(next_pts[i].x - trk.flow_pts[i].x);
                            dys.push_back(next_pts[i].y - trk.flow_pts[i].y);
                            surviving_pts.push_back(next_pts[i]);
                        }
                    }
                }

                if (dxs.size() >= 3) {
                    dx = medianOf(dxs);
                    dy = medianOf(dys);

                    // Move sub-pixel box smoothly with zero integer quantization lag
                    trk.smooth_box.x = std::clamp(trk.smooth_box.x + dx, 0.0f, std::max(0.0f, frame_w - trk.smooth_box.width));
                    trk.smooth_box.y = std::clamp(trk.smooth_box.y + dy, 0.0f, std::max(0.0f, frame_h - trk.smooth_box.height));

                    trk.face.box = cv::Rect(
                        static_cast<int>(std::round(trk.smooth_box.x)),
                        static_cast<int>(std::round(trk.smooth_box.y)),
                        static_cast<int>(std::round(trk.smooth_box.width)),
                        static_cast<int>(std::round(trk.smooth_box.height))
                    );

                    if (trk.face.has_landmarks) {
                        for (auto& lm : trk.face.landmarks) {
                            lm.x += dx;
                            lm.y += dy;
                        }
                    }
                    trk.flow_pts = std::move(surviving_pts);
                }
            }

            // Record this frame's displacement for Delay Compensation
            trk.delta_history.push_back({current_seq, dx, dy});
            while (trk.delta_history.size() > 30) {
                trk.delta_history.pop_front();
            }

            // Refresh features if too few survived
            if (trk.flow_pts.size() < 6) {
                trk.flow_pts = extractFaceFeatures(gray, trk.smooth_box, trk.face.landmarks, trk.face.has_landmarks);
            }
        }
    }

    // 2. Delay-Compensated Fusion with async AI Worker detections
    if (has_new_detections) {
        std::vector<int> used_ids;
        for (const auto& det : ai_faces) {
            const cv::Rect2f raw_det_box(
                static_cast<float>(det.box.x),
                static_cast<float>(det.box.y),
                static_cast<float>(det.box.width),
                static_cast<float>(det.box.height)
            );

            int best_id = -1;
            float best_score = 0.0f;
            cv::Rect2f best_compensated_box = raw_det_box;
            float best_comp_dx = 0.0f;
            float best_comp_dy = 0.0f;

            for (const auto& [tid, trk] : tracks_) {
                if (std::find(used_ids.begin(), used_ids.end(), tid) != used_ids.end()) {
                    continue;
                }

                // Forward-project raw_det_box from ai_frame_seq -> current_seq using this track's Optical Flow history!
                float comp_dx = 0.0f;
                float comp_dy = 0.0f;
                if (enable_predict) {
                    for (const auto& fd : trk.delta_history) {
                        if (fd.seq > ai_frame_seq && fd.seq <= current_seq) {
                            comp_dx += fd.dx;
                            comp_dy += fd.dy;
                        }
                    }
                }

                cv::Rect2f projected = raw_det_box;
                projected.x = std::clamp(projected.x + comp_dx, 0.0f, std::max(0.0f, frame_w - projected.width));
                projected.y = std::clamp(projected.y + comp_dy, 0.0f, std::max(0.0f, frame_h - projected.height));

                const float iou = computeIoU(projected, trk.smooth_box);
                if (iou > best_score) {
                    best_score = iou;
                    best_id = tid;
                    best_compensated_box = projected;
                    best_comp_dx = comp_dx;
                    best_comp_dy = comp_dy;
                }
            }

            if (best_id != -1 && best_score >= 0.15f) {
                auto& trk = tracks_[best_id];
                used_ids.push_back(best_id);

                // Gently correct any tiny Optical Flow drift using the forward-projected AI box
                // (No rubber-banding because best_compensated_box is already at current_seq!)
                const float pos_alpha = enable_predict ? 0.35f : 1.0f;
                const float size_alpha = enable_predict ? 0.45f : 1.0f;

                trk.smooth_box.x = pos_alpha * best_compensated_box.x + (1.0f - pos_alpha) * trk.smooth_box.x;
                trk.smooth_box.y = pos_alpha * best_compensated_box.y + (1.0f - pos_alpha) * trk.smooth_box.y;
                trk.smooth_box.width = size_alpha * best_compensated_box.width + (1.0f - size_alpha) * trk.smooth_box.width;
                trk.smooth_box.height = size_alpha * best_compensated_box.height + (1.0f - size_alpha) * trk.smooth_box.height;

                trk.face = det;
                trk.face.track_id = best_id;
                trk.face.box = cv::Rect(
                    static_cast<int>(std::round(trk.smooth_box.x)),
                    static_cast<int>(std::round(trk.smooth_box.y)),
                    static_cast<int>(std::round(trk.smooth_box.width)),
                    static_cast<int>(std::round(trk.smooth_box.height))
                );

                // Also forward-project the 5 landmarks so eye/nose dots stick tightly too!
                if (enable_predict && trk.face.has_landmarks) {
                    for (auto& lm : trk.face.landmarks) {
                        lm.x += best_comp_dx;
                        lm.y += best_comp_dy;
                    }
                }

                trk.last_seen = now;
                if (enable_predict) {
                    trk.flow_pts = extractFaceFeatures(gray, trk.smooth_box, trk.face.landmarks, trk.face.has_landmarks);
                } else {
                    trk.flow_pts.clear();
                    trk.delta_history.clear();
                }
            } else {
                const int tid = next_id_++;
                used_ids.push_back(tid);
                InternalTrack new_trk;
                new_trk.face = det;
                new_trk.face.track_id = tid;
                new_trk.smooth_box = raw_det_box;
                new_trk.last_seen = now;
                if (enable_predict) {
                    new_trk.flow_pts = extractFaceFeatures(gray, raw_det_box, det.landmarks, det.has_landmarks);
                }
                tracks_[tid] = std::move(new_trk);
            }
        }
    }

    if (enable_predict) {
        prev_gray_ = std::move(gray);
    } else {
        prev_gray_.release();
    }

    // 3. Prune expired tracks and return active faces
    std::vector<TrackedFace> active;
    for (auto it = tracks_.begin(); it != tracks_.end();) {
        const double elapsed = std::chrono::duration<double>(now - it->second.last_seen).count();
        if (elapsed > active_decay) {
            it = tracks_.erase(it);
        } else {
            active.push_back(it->second.face);
            ++it;
        }
    }

    return active;
}

} // namespace fr
