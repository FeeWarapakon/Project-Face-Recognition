#include "face_tracker.hpp"
#include <algorithm>
#include <cmath>

namespace fr {

OpticalFlowFaceTracker::OpticalFlowFaceTracker(double decay_sec)
    : decay_sec_(decay_sec) {}

float OpticalFlowFaceTracker::computeIoU(const cv::Rect& a, const cv::Rect& b) {
    const int x1 = std::max(a.x, b.x);
    const int y1 = std::max(a.y, b.y);
    const int x2 = std::min(a.x + a.width, b.x + b.width);
    const int y2 = std::min(a.y + a.height, b.y + b.height);
    const int inter = std::max(0, x2 - x1) * std::max(0, y2 - y1);
    const int union_area = std::max(1, a.area() + b.area() - inter);
    return static_cast<float>(inter) / static_cast<float>(union_area);
}

std::vector<cv::Point2f> OpticalFlowFaceTracker::generateFeaturePoints(
    const cv::Rect& box,
    const std::array<cv::Point2f, 5>& landmarks,
    bool has_landmarks) {
    std::vector<cv::Point2f> pts;
    pts.reserve(10);

    if (has_landmarks) {
        for (const auto& lm : landmarks) {
            pts.push_back(lm);
        }
    }

    const float cx = box.x + box.width * 0.5f;
    const float cy = box.y + box.height * 0.5f;
    pts.emplace_back(cx, cy);
    pts.emplace_back(cx, box.y + box.height * 0.25f);
    pts.emplace_back(cx, box.y + box.height * 0.75f);
    pts.emplace_back(box.x + box.width * 0.25f, cy);
    pts.emplace_back(box.x + box.width * 0.75f, cy);
    return pts;
}

static float medianOf(std::vector<float>& vals) {
    const size_t n = vals.size();
    std::nth_element(vals.begin(), vals.begin() + n / 2, vals.end());
    return vals[n / 2];
}

std::vector<TrackedFace> OpticalFlowFaceTracker::updateFrame(
    const cv::Mat& frame_bgr,
    const std::vector<TrackedFace>& ai_faces,
    bool has_new_detections) {
    const auto now = std::chrono::steady_clock::now();
    const int frame_w = frame_bgr.cols;
    const int frame_h = frame_bgr.rows;

    cv::Mat gray;
    cv::cvtColor(frame_bgr, gray, cv::COLOR_BGR2GRAY);

    // 1. Lucas-Kanade Sparse Optical Flow on every camera frame (Zero-Lag 60 FPS)
    if (!prev_gray_.empty() && prev_gray_.size() == gray.size() && !tracks_.empty()) {
        for (auto& [tid, trk] : tracks_) {
            if (trk.flow_pts.size() < 3) {
                continue;
            }
            std::vector<cv::Point2f> next_pts;
            std::vector<uchar> status;
            std::vector<float> err;

            cv::calcOpticalFlowPyrLK(
                prev_gray_, gray, trk.flow_pts, next_pts, status, err,
                cv::Size(15, 15), 2,
                cv::TermCriteria(cv::TermCriteria::COUNT | cv::TermCriteria::EPS, 10, 0.03)
            );

            std::vector<float> dxs;
            std::vector<float> dys;
            std::vector<cv::Point2f> surviving_pts;
            dxs.reserve(status.size());
            dys.reserve(status.size());
            surviving_pts.reserve(status.size());

            for (size_t i = 0; i < status.size(); ++i) {
                if (status[i]) {
                    dxs.push_back(next_pts[i].x - trk.flow_pts[i].x);
                    dys.push_back(next_pts[i].y - trk.flow_pts[i].y);
                    surviving_pts.push_back(next_pts[i]);
                }
            }

            if (dxs.size() >= 3) {
                const float dx = medianOf(dxs);
                const float dy = medianOf(dys);

                cv::Rect& b = trk.face.box;
                b.x = std::clamp(static_cast<int>(std::round(b.x + dx)), 0, std::max(0, frame_w - b.width));
                b.y = std::clamp(static_cast<int>(std::round(b.y + dy)), 0, std::max(0, frame_h - b.height));

                if (trk.face.has_landmarks) {
                    for (auto& lm : trk.face.landmarks) {
                        lm.x += dx;
                        lm.y += dy;
                    }
                }
                trk.flow_pts = std::move(surviving_pts);
            }
        }
    }

    // 2. Fuse fresh Deep AI Worker detections with active Optical Flow tracks
    if (has_new_detections) {
        std::vector<int> used_ids;
        for (const auto& det : ai_faces) {
            int best_id = -1;
            float best_iou = 0.0f;

            for (const auto& [tid, trk] : tracks_) {
                if (std::find(used_ids.begin(), used_ids.end(), tid) != used_ids.end()) {
                    continue;
                }
                const float iou = computeIoU(det.box, trk.face.box);
                if (iou > best_iou) {
                    best_iou = iou;
                    best_id = tid;
                }
            }

            if (best_id != -1 && best_iou >= 0.15f) {
                auto& trk = tracks_[best_id];
                used_ids.push_back(best_id);

                // Smooth blend between Optical Flow prediction and AI detection
                constexpr float alpha = 0.70f;
                const cv::Rect& prev_b = trk.face.box;
                cv::Rect blended;
                blended.x = static_cast<int>(std::round(alpha * det.box.x + (1.0f - alpha) * prev_b.x));
                blended.y = static_cast<int>(std::round(alpha * det.box.y + (1.0f - alpha) * prev_b.y));
                blended.width = static_cast<int>(std::round(alpha * det.box.width + (1.0f - alpha) * prev_b.width));
                blended.height = static_cast<int>(std::round(alpha * det.box.height + (1.0f - alpha) * prev_b.height));

                trk.face = det;
                trk.face.track_id = best_id;
                trk.face.box = blended;
                trk.last_seen = now;
                trk.flow_pts = generateFeaturePoints(blended, det.landmarks, det.has_landmarks);
            } else {
                const int tid = next_id_++;
                used_ids.push_back(tid);
                InternalTrack new_trk;
                new_trk.face = det;
                new_trk.face.track_id = tid;
                new_trk.last_seen = now;
                new_trk.flow_pts = generateFeaturePoints(det.box, det.landmarks, det.has_landmarks);
                tracks_[tid] = std::move(new_trk);
            }
        }
    }

    prev_gray_ = std::move(gray);

    // 3. Prune expired tracks and return active faces
    std::vector<TrackedFace> active;
    for (auto it = tracks_.begin(); it != tracks_.end();) {
        const double elapsed = std::chrono::duration<double>(now - it->second.last_seen).count();
        if (elapsed > decay_sec_) {
            it = tracks_.erase(it);
        } else {
            active.push_back(it->second.face);
            ++it;
        }
    }

    return active;
}

} // namespace fr
