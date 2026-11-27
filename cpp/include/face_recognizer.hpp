#pragma once

#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>
#include <memory>
#include <string>
#include <vector>

#ifdef USE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

namespace fr {

/**
 * MobileFaceNet Feature Extractor (128-d L2-normalized Embeddings).
 * - Prioritizes mobilefacenet_int8.onnx when ONNX Runtime C++ is enabled.
 * - Provides automatic OpenCV DNN fallback so the project builds out-of-the-box on any SBC/PC.
 */
class FaceRecognizer {
public:
    FaceRecognizer();
    ~FaceRecognizer();

    bool init(const std::string& model_dir, int num_threads = 4);

    /**
     * Extracts a 128-d L2-normalized embedding from a 112x112 BGR aligned face.
     */
    std::vector<float> extractEmbedding(const cv::Mat& aligned_face_bgr);

    bool isReady() const { return ready_; }
    bool isInt8() const { return is_int8_; }
    std::string backendName() const { return backend_name_; }
    std::string modelName() const { return model_name_; }

private:
    bool ready_ = false;
    bool is_int8_ = false;
    std::string backend_name_ = "none";
    std::string model_name_;
    cv::Size input_size_{112, 112};

    // OpenCV DNN backend
    cv::dnn::Net cv_net_;
    bool use_cv_dnn_ = false;

#ifdef USE_ONNXRUNTIME
    std::unique_ptr<Ort::Env> ort_env_;
    std::unique_ptr<Ort::Session> ort_session_;
    std::string ort_input_name_;
    std::string ort_output_name_;
    bool use_ort_ = false;
#endif
};

} // namespace fr
