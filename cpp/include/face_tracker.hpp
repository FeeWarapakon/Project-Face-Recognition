#pragma once

#include "types.hpp"
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <vector>

namespace fr {

/**
 * Delay-Compensated Sparse Optical Flow Face Tracker (AR-Grade Zero-Lag 60 FPS).
 *
 * Key technique:
 * 1. Tracks high-contrast facial features (landmarks + Shi-Tomasi corners) on every camera frame (< 1.5 ms).
 * 2. Records per-frame displacement (dx, dy) in a ring buffer keyed by frame_seq.
 * 3. When the async AI Worker finishes an older frame (ai_frame_seq), its detection box is
 *    forward-projected using the accumulated Optical Flow displacements from ai_frame_seq -> current_seq.
 *    This completely eliminates the "rubber-banding / lagging behind" pull-back effect!
 */
class OpticalFlowFaceTracker {
public:
    explicit OpticalFlowFaceTracker(double decay_sec = 0.60);

    /**
     * Updates active tracks on every camera frame.
     * @param frame_bgr           Current camera frame (at current_seq)
     * @param current_seq         Monotonic sequence number of frame_bgr
     * @param ai_faces            Latest faces from async AI Worker
     * @param ai_frame_seq        The frame_seq that the AI Worker actually ran on
     * @param has_new_detections  True only on the frame when a fresh AI result arrives
     */
    std::vector<TrackedFace> updateFrame(const cv::Mat& frame_bgr,
                                         uint64_t current_seq,
                                         const std::vector<TrackedFace>& ai_faces,
                                         uint64_t ai_frame_seq,
                                         bool has_new_detections,
                                         bool enable_predict = true,
                                         double decay_sec_override = -1.0);

private:
    struct FrameDelta {
        uint64_t seq = 0;
        float dx = 0.0f;
        float dy = 0.0f;
    };

    struct InternalTrack {
        TrackedFace face;
        cv::Rect2f smooth_box; // Sub-pixel precision box to avoid integer jitter
        std::vector<cv::Point2f> flow_pts;
        std::deque<FrameDelta> delta_history;
        std::chrono::steady_clock::time_point last_seen;
    };

    static float computeIoU(const cv::Rect2f& a, const cv::Rect2f& b);
    static std::vector<cv::Point2f> extractFaceFeatures(const cv::Mat& gray,
                                                        const cv::Rect2f& box,
                                                        const std::array<cv::Point2f, 5>& landmarks,
                                                        bool has_landmarks);

    std::map<int, InternalTrack> tracks_;
    cv::Mat prev_gray_;
    double decay_sec_;
    int next_id_ = 1;
};

} // namespace fr
