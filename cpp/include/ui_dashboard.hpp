#pragma once

#include "database.hpp"
#include "types.hpp"

#include <functional>
#include <string>
#include <vector>

struct GLFWwindow;

namespace fr {

struct TelemetrySnapshot {
    double cam_fps = 0.0;
    double disp_fps = 0.0;
    double ai_ms = 0.0;
    int frame_w = 640;
    int frame_h = 480;
    bool is_int8 = false;
    std::string backend_name;
    std::string model_name;
};

struct DashboardCallbacks {
    // Called when user clicks "Capture & Enroll" on Register tab
    std::function<bool(const std::string& name,
                       const std::string& student_id,
                       const std::string& role,
                       std::string& out_message)> on_enroll;

    // Called when user clicks "Switch Camera" on Settings tab
    std::function<bool(int new_camera_id, std::string& out_message)> on_switch_camera;
};

/**
 * Lightweight Native Dashboard UI powered by Dear ImGui + GLFW + OpenGL2.
 * - Replaces the Web Dashboard with Zero-Copy GPU Texture rendering (< 80 MB RAM, 60 FPS).
 * - Compatible with Raspberry Pi 3 (OpenGL 2.1), Pi 5 (OpenGL 3.1), Orange Pi, Jetson, and Windows.
 */
class NativeDashboardUI {
public:
    NativeDashboardUI() = default;
    ~NativeDashboardUI();

    bool init(const std::string& title, int window_w = 1200, int window_h = 720);
    void shutdown();

    bool shouldClose() const;

    /**
     * Uploads the latest BGR video frame to GPU texture and renders one full UI frame.
     */
    void renderFrame(const cv::Mat& display_bgr,
                     const std::vector<TrackedFace>& active_faces,
                     const TelemetrySnapshot& telemetry,
                     AppConfig& cfg,
                     FaceDatabase& db,
                     const DashboardCallbacks& callbacks);

private:
    void applyModernDarkTheme();
    void updateVideoTexture(const cv::Mat& frame_bgr);
    void renderTopBar(const TelemetrySnapshot& telemetry, int enrolled_count);
    void renderLiveMonitorTab(const std::vector<TrackedFace>& active_faces,
                              const TelemetrySnapshot& telemetry,
                              FaceDatabase& db);
    void renderRegisterTab(const std::vector<TrackedFace>& active_faces,
                           const DashboardCallbacks& callbacks);
    void renderPersonsTab(FaceDatabase& db);
    void renderSettingsTab(const TelemetrySnapshot& telemetry,
                           AppConfig& cfg,
                           FaceDatabase& db,
                           const DashboardCallbacks& callbacks);
    void drawVideoPanel(float avail_w, float avail_h);

    GLFWwindow* window_ = nullptr;
    unsigned int video_tex_id_ = 0;
    int tex_w_ = 0;
    int tex_h_ = 0;
    cv::Mat rgb_buf_;

    // UI State
    int active_tab_ = 0; // 0: Live, 1: Register, 2: Persons, 3: Settings
    char enroll_name_[128] = "";
    char enroll_sid_[64] = "";
    int enroll_role_idx_ = 0;
    std::string enroll_status_msg_;
    bool enroll_status_ok_ = false;

    char search_buf_[128] = "";
    int pending_cam_id_ = 0;
    std::string settings_status_msg_;

    // Cached DB lists for smooth 60 FPS UI without querying SQLite every frame
    std::vector<PersonSummary> persons_cache_;
    std::vector<RecognitionLogEntry> logs_cache_;
    double last_db_refresh_sec_ = -10.0;
    bool force_db_refresh_ = true;

    // Performance graph ring buffers
    std::vector<float> fps_history_;
    std::vector<float> ai_ms_history_;
};

} // namespace fr
