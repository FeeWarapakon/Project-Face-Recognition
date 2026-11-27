#include "face_detector.hpp"
#include <algorithm>
#include <iostream>

namespace fr {

bool FaceDetector::init(const std::string& yunet_model_path,
                        float score_threshold,
                        float nms_threshold,
                        int top_k) {
    try {
        detector_ = cv::FaceDetectorYN::create(
            yunet_model_path,
            "",
            cv::Size(320, 240),
            score_threshold,
            nms_threshold,
            top_k
        );
        last_input_size_ = cv::Size(320, 240);
        ready_ = !detector_.empty();
        if (ready_) {
            std::cout << "[OK] FaceDetector (YuNet ONNX C++) loaded: " << yunet_model_path << "\n";
        }
    } catch (const cv::Exception& ex) {
        std::cerr << "[ERROR] Failed to initialize YuNet FaceDetector: " << ex.what() << "\n";
        ready_ = false;
    }
    return ready_;
}

std::vector<FaceDetection> FaceDetector::detect(const cv::Mat& frame_bgr, int max_detect_width) {
    std::vector<FaceDetection> results;
    if (!ready_ || frame_bgr.empty()) {
        return results;
    }

    const int orig_w = frame_bgr.cols;
    const int orig_h = frame_bgr.rows;

    float scale = 1.0f;
    cv::Mat proc_img;
    if (max_detect_width > 0 && orig_w > max_detect_width) {
        scale = static_cast<float>(max_detect_width) / static_cast<float>(orig_w);
        const int det_w = std::max(32, static_cast<int>(orig_w * scale));
        const int det_h = std::max(32, static_cast<int>(orig_h * scale));
        cv::resize(frame_bgr, proc_img, cv::Size(det_w, det_h), 0, 0, cv::INTER_LINEAR);
    } else {
        proc_img = frame_bgr;
    }

    const cv::Size cur_size(proc_img.cols, proc_img.rows);
    if (cur_size != last_input_size_) {
        detector_->setInputSize(cur_size);
        last_input_size_ = cur_size;
    }

    cv::Mat faces_mat;
    detector_->detect(proc_img, faces_mat);
    if (faces_mat.empty() || faces_mat.rows == 0) {
        return results;
    }

    const float inv_scale = 1.0f / scale;
    results.reserve(static_cast<size_t>(faces_mat.rows));

    for (int i = 0; i < faces_mat.rows; ++i) {
        const float conf = faces_mat.at<float>(i, 14);
        if (conf < 0.50f) {
            continue;
        }

        const int x = std::clamp(static_cast<int>(faces_mat.at<float>(i, 0) * inv_scale), 0, orig_w - 1);
        const int y = std::clamp(static_cast<int>(faces_mat.at<float>(i, 1) * inv_scale), 0, orig_h - 1);
        const int bw = static_cast<int>(faces_mat.at<float>(i, 2) * inv_scale);
        const int bh = static_cast<int>(faces_mat.at<float>(i, 3) * inv_scale);
        const int x2 = std::clamp(x + bw, x + 1, orig_w);
        const int y2 = std::clamp(y + bh, y + 1, orig_h);

        if ((x2 - x) < 16 || (y2 - y) < 16) {
            continue;
        }

        FaceDetection det;
        det.box = cv::Rect(x, y, x2 - x, y2 - y);
        det.confidence = conf;
        det.has_landmarks = true;
        // YuNet landmark order: right_eye, left_eye, nose, right_mouth, left_mouth
        for (int k = 0; k < 5; ++k) {
            det.landmarks[k] = cv::Point2f(
                faces_mat.at<float>(i, 4 + k * 2) * inv_scale,
                faces_mat.at<float>(i, 5 + k * 2) * inv_scale
            );
        }
        results.push_back(det);
    }

    return results;
}

cv::Mat FaceDetector::alignFace5Pts(const cv::Mat& frame_bgr,
                                    const std::array<cv::Point2f, 5>& landmarks,
                                    int target_size) {
    // Canonical 112x112 ArcFace / InsightFace template
    static const std::vector<cv::Point2f> kRef5Pts112 = {
        {38.2946f, 51.6963f},
        {73.5318f, 51.6963f},
        {56.0252f, 71.7366f},
        {41.5493f, 92.3655f},
        {70.7299f, 92.3655f}
    };

    std::vector<cv::Point2f> dst_pts = kRef5Pts112;
    if (target_size != 112) {
        const float ratio = static_cast<float>(target_size) / 112.0f;
        for (auto& pt : dst_pts) {
            pt.x *= ratio;
            pt.y *= ratio;
        }
    }

    std::vector<cv::Point2f> src_pts(landmarks.begin(), landmarks.end());
    cv::Mat M = cv::estimateAffinePartial2D(src_pts, dst_pts, cv::noArray(), cv::LMEDS);
    if (!M.empty()) {
        cv::Mat aligned;
        cv::warpAffine(frame_bgr, aligned, M, cv::Size(target_size, target_size),
                       cv::INTER_CUBIC, cv::BORDER_REFLECT);
        return aligned;
    }

    cv::Mat resized;
    cv::resize(frame_bgr, resized, cv::Size(target_size, target_size), 0, 0, cv::INTER_CUBIC);
    return resized;
}

} // namespace fr
