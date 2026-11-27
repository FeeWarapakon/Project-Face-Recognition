#pragma once

#include "types.hpp"
#include <opencv2/objdetect/face.hpp>
#include <string>
#include <vector>

namespace fr {

/**
 * Lightweight YuNet ONNX Face Detector + 5-Point ArcFace Alignment.
 * Supports adaptive input downscaling for high-FPS edge inference (Pi 3 / Pi 5 / x64).
 */
class FaceDetector {
public:
    FaceDetector() = default;

    bool init(const std::string& yunet_model_path,
              float score_threshold = 0.55f,
              float nms_threshold = 0.30f,
              int top_k = 10);

    std::vector<FaceDetection> detect(const cv::Mat& frame_bgr, int max_detect_width = 400);

    /**
     * Canonical 112x112 5-point similarity affine alignment (ArcFace / MobileFaceNet template).
     */
    static cv::Mat alignFace5Pts(const cv::Mat& frame_bgr,
                                 const std::array<cv::Point2f, 5>& landmarks,
                                 int target_size = 112);

    bool isReady() const { return ready_; }

private:
    cv::Ptr<cv::FaceDetectorYN> detector_;
    cv::Size last_input_size_{0, 0};
    bool ready_ = false;
};

} // namespace fr
