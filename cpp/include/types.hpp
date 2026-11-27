#pragma once

#include <opencv2/opencv.hpp>
#include <array>
#include <string>
#include <vector>

namespace fr {

struct FaceDetection {
    cv::Rect box;
    float confidence = 0.0f;
    std::array<cv::Point2f, 5> landmarks{};
    bool has_landmarks = false;
};

struct MatchResult {
    int person_id = -1;
    std::string name = "Unknown";
    std::string student_id;
    std::string role;
    float similarity = 0.0f;
    std::string status = "UNKNOWN"; // "KNOWN" | "UNKNOWN" | "SCANNING"
};

struct TrackedFace {
    int track_id = -1;
    cv::Rect box;
    std::array<cv::Point2f, 5> landmarks{};
    bool has_landmarks = false;
    float confidence = 0.0f;
    MatchResult match;
    double detect_ms = 0.0;
    double embed_ms = 0.0;
    double match_ms = 0.0;
};

struct EmbeddingRecord {
    int person_id = -1;
    std::string name;
    std::string student_id;
    std::string role;
    std::vector<float> embedding; // L2-normalized 128-d vector
};

struct PersonSummary {
    int id = -1;
    std::string name;
    std::string student_id;
    std::string role;
    int embedding_count = 0;
    std::string created_at;
};

struct RecognitionLogEntry {
    int id = -1;
    std::string timestamp;
    std::string matched_name;
    float similarity = 0.0f;
    std::string status;
    double total_latency_ms = 0.0;
};

struct AppConfig {
    int camera_id = 0;
    int width = 640;
    int height = 480;
    int target_fps = 60;
    int detect_width = 400;       // Downscale width for YuNet (256 for Pi 3)
    float threshold = 0.60f;
    bool flip_horizontal = true;
    bool headless = false;
    bool pi3_mode = false;
    std::string model_dir = "../Antigravity";
    std::string db_path = "../Antigravity/face_recognition.db";
};

} // namespace fr
