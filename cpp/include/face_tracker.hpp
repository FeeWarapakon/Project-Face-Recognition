#pragma once

#include "types.hpp"
#include <chrono>
#include <map>
#include <vector>

namespace fr {

/**
 * Zero-Lag Sparse Optical Flow Face Tracker (Lucas-Kanade + IoU Fusion).
 * Tracks facial landmarks on every camera frame (< 1.5 ms) so bounding boxes
 * glide smoothly at full native camera FPS (30-60 FPS) between AI detections.
 */
class OpticalFlowFaceTracker {
public:
    explicit OpticalFlowFaceTracker(double decay_sec = 0.55);

    /**
     * Updates active tracks on every camera frame.
     * Pass `has_new_detections = true` only when a fresh AI worker result arrives.
     */
    std::vector<TrackedFace> updateFrame(const cv::Mat& frame_bgr,
                                         const std::vector<TrackedFace>& ai_faces,
                                         bool has_new_detections);

private:
    struct InternalTrack {
        TrackedFace face;
        std::vector<cv::Point2f> flow_pts;
        std::chrono::steady_clock::time_point last_seen;
    };

    static float computeIoU(const cv::Rect& a, const cv::Rect& b);
    static std::vector<cv::Point2f> generateFeaturePoints(const cv::Rect& box,
                                                          const std::array<cv::Point2f, 5>& landmarks,
                                                          bool has_landmarks);

    std::map<int, InternalTrack> tracks_;
    cv::Mat prev_gray_;
    double decay_sec_;
    int next_id_ = 1;
};

} // namespace fr
