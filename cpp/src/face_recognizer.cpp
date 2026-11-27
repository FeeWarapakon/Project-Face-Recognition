#include "face_recognizer.hpp"
#include <cmath>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace fr {

FaceRecognizer::FaceRecognizer() = default;
FaceRecognizer::~FaceRecognizer() = default;

bool FaceRecognizer::init(const std::string& model_dir, int num_threads) {
    const std::string int8_path = (fs::path(model_dir) / "mobilefacenet_int8.onnx").string();
    const std::string fp32_path = (fs::path(model_dir) / "mobilefacenet.onnx").string();

#ifdef USE_ONNXRUNTIME
    // 1. Prioritize ONNX Runtime C++ with INT8 Quantized MobileFaceNet
    const std::vector<std::string> ort_candidates = {int8_path, fp32_path};
    for (const auto& candidate : ort_candidates) {
        if (!fs::exists(candidate)) {
            continue;
        }
        try {
            ort_env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_ERROR, "FaceRecognitionNative");
            Ort::SessionOptions opts;
            opts.SetIntraOpNumThreads(std::max(1, num_threads));
            opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

#ifdef _WIN32
            std::wstring wpath(candidate.begin(), candidate.end());
            ort_session_ = std::make_unique<Ort::Session>(*ort_env_, wpath.c_str(), opts);
#else
            ort_session_ = std::make_unique<Ort::Session>(*ort_env_, candidate.c_str(), opts);
#endif
            Ort::AllocatorWithDefaultOptions allocator;
            auto in_name = ort_session_->GetInputNameAllocated(0, allocator);
            auto out_name = ort_session_->GetOutputNameAllocated(0, allocator);
            ort_input_name_ = in_name.get();
            ort_output_name_ = out_name.get();

            use_ort_ = true;
            ready_ = true;
            model_name_ = fs::path(candidate).filename().string();
            is_int8_ = (model_name_.find("int8") != std::string::npos);
            backend_name_ = is_int8_ ? "ONNX Runtime C++ (INT8)" : "ONNX Runtime C++ (FP32)";

            std::cout << "[OK] FaceRecognizer (" << backend_name_ << ") loaded: " << model_name_ << "\n";
            return true;
        } catch (const std::exception& ex) {
            std::cerr << "[WARN] ONNX Runtime could not load " << candidate << ": " << ex.what() << "\n";
        }
    }
#endif

    // 2. Zero-Dependency OpenCV DNN Fallback (works on any SBC/PC with libopencv-dev)
    // Note: Older OpenCV versions may not support QDQ INT8 ops, so we try INT8 then FP32 gracefully.
    const std::vector<std::string> cv_candidates = {int8_path, fp32_path};
    for (const auto& candidate : cv_candidates) {
        if (!fs::exists(candidate)) {
            continue;
        }
        try {
            cv::dnn::Net net = cv::dnn::readNetFromONNX(candidate);
            if (!net.empty()) {
                // Quick dry-run test to ensure all operators (including QDQ) are supported
                cv::Mat dummy(112, 112, CV_8UC3, cv::Scalar(128, 128, 128));
                cv::Mat blob = cv::dnn::blobFromImage(dummy, 1.0 / 128.0, input_size_,
                                                      cv::Scalar(127.5, 127.5, 127.5), true, false);
                net.setInput(blob);
                cv::Mat out = net.forward();
                if (!out.empty() && out.total() >= 128) {
                    cv_net_ = std::move(net);
                    use_cv_dnn_ = true;
                    ready_ = true;
                    model_name_ = fs::path(candidate).filename().string();
                    is_int8_ = (model_name_.find("int8") != std::string::npos);
                    backend_name_ = is_int8_ ? "OpenCV DNN C++ (INT8)" : "OpenCV DNN C++ (FP32)";
                    std::cout << "[OK] FaceRecognizer (" << backend_name_ << ") loaded: " << model_name_ << "\n";
                    if (!is_int8_) {
                        std::cout << "[i] Tip: Build with -DUSE_ONNXRUNTIME=ON to enable INT8 QDQ acceleration.\n";
                    }
                    return true;
                }
            }
        } catch (const cv::Exception&) {
            // Gracefully try next model candidate
        }
    }

    std::cerr << "[ERROR] Failed to load MobileFaceNet model from directory: " << model_dir << "\n";
    return false;
}

std::vector<float> FaceRecognizer::extractEmbedding(const cv::Mat& aligned_face_bgr) {
    std::vector<float> embedding;
    if (!ready_ || aligned_face_bgr.empty()) {
        return embedding;
    }

#ifdef USE_ONNXRUNTIME
    if (use_ort_ && ort_session_) {
        cv::Mat resized;
        if (aligned_face_bgr.size() != input_size_) {
            cv::resize(aligned_face_bgr, resized, input_size_, 0, 0, cv::INTER_AREA);
        } else {
            resized = aligned_face_bgr;
        }

        cv::Mat rgb;
        cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);

        // NCHW float32 normalized to [-1, 1]
        const int h = input_size_.height;
        const int w = input_size_.width;
        const size_t plane = static_cast<size_t>(h * w);
        std::vector<float> input_tensor(3 * plane);

        for (int y = 0; y < h; ++y) {
            const cv::Vec3b* row_ptr = rgb.ptr<cv::Vec3b>(y);
            for (int x = 0; x < w; ++x) {
                const size_t idx = static_cast<size_t>(y * w + x);
                input_tensor[0 * plane + idx] = (static_cast<float>(row_ptr[x][0]) - 127.5f) / 128.0f;
                input_tensor[1 * plane + idx] = (static_cast<float>(row_ptr[x][1]) - 127.5f) / 128.0f;
                input_tensor[2 * plane + idx] = (static_cast<float>(row_ptr[x][2]) - 127.5f) / 128.0f;
            }
        }

        const std::array<int64_t, 4> input_shape = {1, 3, h, w};
        Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value input_val = Ort::Value::CreateTensor<float>(
            mem_info, input_tensor.data(), input_tensor.size(), input_shape.data(), input_shape.size());

        const char* in_names[] = {ort_input_name_.c_str()};
        const char* out_names[] = {ort_output_name_.c_str()};
        auto outputs = ort_session_->Run(Ort::RunOptions{nullptr}, in_names, &input_val, 1, out_names, 1);

        const float* out_ptr = outputs[0].GetTensorData<float>();
        const auto shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        size_t dim = 1;
        for (auto s : shape) {
            if (s > 0) dim *= static_cast<size_t>(s);
        }

        embedding.assign(out_ptr, out_ptr + dim);
    } else
#endif
    if (use_cv_dnn_) {
        cv::Mat blob = cv::dnn::blobFromImage(
            aligned_face_bgr,
            1.0 / 128.0,
            input_size_,
            cv::Scalar(127.5, 127.5, 127.5),
            true,  // BGR -> RGB
            false
        );
        cv_net_.setInput(blob);
        cv::Mat out = cv_net_.forward();
        const float* ptr = out.ptr<float>(0);
        embedding.assign(ptr, ptr + out.total());
    }

    // L2 Normalization
    double norm_sq = 0.0;
    for (float v : embedding) {
        norm_sq += static_cast<double>(v) * static_cast<double>(v);
    }
    const float norm = static_cast<float>(std::sqrt(norm_sq));
    if (norm > 1e-6f) {
        for (float& v : embedding) {
            v /= norm;
        }
    }

    return embedding;
}

} // namespace fr
