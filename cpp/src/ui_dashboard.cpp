#include "ui_dashboard.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include <GLFW/glfw3.h>
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iostream>

namespace fr {

static std::string toLowerStr(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

NativeDashboardUI::~NativeDashboardUI() {
    shutdown();
}

bool NativeDashboardUI::init(const std::string& title, int window_w, int window_h) {
    if (!glfwInit()) {
        std::cerr << "[ERROR] Failed to initialize GLFW.\n";
        return false;
    }

    window_ = glfwCreateWindow(window_w, window_h, title.c_str(), nullptr, nullptr);
    if (!window_) {
        std::cerr << "[ERROR] Failed to create GLFW window (is DISPLAY set?).\n";
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1); // V-Sync smooth 60 FPS without tearing

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // Do not clutter workspace with imgui.ini

    applyModernDarkTheme();

    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL2_Init();

    glGenTextures(1, &video_tex_id_);
    glBindTexture(GL_TEXTURE_2D, video_tex_id_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    fps_history_.assign(120, 0.0f);
    ai_ms_history_.assign(120, 0.0f);

    std::cout << "[OK] Native Dashboard UI (Dear ImGui + OpenGL) initialized (" << window_w << "x" << window_h << ").\n";
    return true;
}

void NativeDashboardUI::shutdown() {
    if (video_tex_id_ != 0) {
        glDeleteTextures(1, &video_tex_id_);
        video_tex_id_ = 0;
    }
    if (window_) {
        ImGui_ImplOpenGL2_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window_);
        glfwTerminate();
        window_ = nullptr;
    }
}

bool NativeDashboardUI::shouldClose() const {
    return !window_ || glfwWindowShouldClose(window_);
}

void NativeDashboardUI::applyModernDarkTheme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 5.0f;
    style.ChildRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.WindowPadding = ImVec2(12, 10);
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(10, 8);

    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg]         = ImVec4(0.06f, 0.09f, 0.14f, 1.00f); // Slate-950
    c[ImGuiCol_ChildBg]          = ImVec4(0.09f, 0.13f, 0.20f, 1.00f); // Slate-900
    c[ImGuiCol_Border]           = ImVec4(0.18f, 0.25f, 0.36f, 0.70f);
    c[ImGuiCol_FrameBg]          = ImVec4(0.12f, 0.17f, 0.26f, 1.00f);
    c[ImGuiCol_FrameBgHovered]   = ImVec4(0.16f, 0.23f, 0.35f, 1.00f);
    c[ImGuiCol_FrameBgActive]    = ImVec4(0.08f, 0.45f, 0.65f, 1.00f);
    c[ImGuiCol_Button]           = ImVec4(0.10f, 0.46f, 0.68f, 1.00f); // Cyan-600
    c[ImGuiCol_ButtonHovered]    = ImVec4(0.14f, 0.58f, 0.82f, 1.00f);
    c[ImGuiCol_ButtonActive]     = ImVec4(0.06f, 0.38f, 0.56f, 1.00f);
    c[ImGuiCol_Header]           = ImVec4(0.11f, 0.42f, 0.62f, 0.80f);
    c[ImGuiCol_HeaderHovered]    = ImVec4(0.14f, 0.52f, 0.74f, 0.90f);
    c[ImGuiCol_Tab]              = ImVec4(0.10f, 0.15f, 0.23f, 1.00f);
    c[ImGuiCol_TabHovered]       = ImVec4(0.14f, 0.50f, 0.72f, 0.90f);
    c[ImGuiCol_TabSelected]      = ImVec4(0.09f, 0.48f, 0.70f, 1.00f);
    c[ImGuiCol_PlotLines]        = ImVec4(0.15f, 0.85f, 0.95f, 1.00f);
    c[ImGuiCol_SliderGrab]       = ImVec4(0.15f, 0.80f, 0.95f, 1.00f);
    c[ImGuiCol_CheckMark]        = ImVec4(0.20f, 0.90f, 0.60f, 1.00f);
}

void NativeDashboardUI::updateVideoTexture(const cv::Mat& frame_bgr) {
    if (frame_bgr.empty() || video_tex_id_ == 0) return;

    cv::cvtColor(frame_bgr, rgb_buf_, cv::COLOR_BGR2RGB);
    if (!rgb_buf_.isContinuous()) {
        rgb_buf_ = rgb_buf_.clone();
    }

    glBindTexture(GL_TEXTURE_2D, video_tex_id_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    if (tex_w_ != rgb_buf_.cols || tex_h_ != rgb_buf_.rows) {
        tex_w_ = rgb_buf_.cols;
        tex_h_ = rgb_buf_.rows;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, tex_w_, tex_h_, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, rgb_buf_.data);
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tex_w_, tex_h_,
                        GL_RGB, GL_UNSIGNED_BYTE, rgb_buf_.data);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void NativeDashboardUI::drawVideoPanel(float avail_w, float avail_h) {
    if (video_tex_id_ == 0 || tex_w_ <= 0 || tex_h_ <= 0) {
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f), "Waiting for camera frame...");
        return;
    }

    const float aspect = static_cast<float>(tex_w_) / static_cast<float>(tex_h_);
    float draw_w = avail_w;
    float draw_h = draw_w / aspect;
    if (draw_h > avail_h) {
        draw_h = avail_h;
        draw_w = draw_h * aspect;
    }

    const float pad_x = std::max(0.0f, (avail_w - draw_w) * 0.5f);
    const float pad_y = std::max(0.0f, (avail_h - draw_h) * 0.5f);
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + pad_x, ImGui::GetCursorPosY() + pad_y));
    ImGui::Image(static_cast<ImTextureID>(static_cast<uintptr_t>(video_tex_id_)), ImVec2(draw_w, draw_h));
}

void NativeDashboardUI::renderTopBar(const TelemetrySnapshot& telemetry, int enrolled_count) {
    ImGui::BeginChild("TopBar", ImVec2(0, 48), true, ImGuiWindowFlags_NoScrollbar);

    ImGui::TextColored(ImVec4(0.20f, 0.88f, 1.00f, 1.0f), "EDGE FACE RECOGNITION");
    ImGui::SameLine();
    ImGui::TextDisabled("| C++17 Native");

    ImGui::SameLine(280.0f);
    auto navBtn = [&](const char* label, int tab_idx) {
        const bool selected = (active_tab_ == tab_idx);
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.56f, 0.80f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.18f, 0.27f, 1.0f));
        }
        if (ImGui::Button(label, ImVec2(125, 28))) {
            active_tab_ = tab_idx;
            force_db_refresh_ = true;
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
    };

    navBtn("Live Monitor", 0);
    navBtn("Register Face", 1);
    navBtn("Persons DB", 2);
    navBtn("Settings", 3);

    // Right-aligned live telemetry pills
    const float right_x = std::max(810.0f, ImGui::GetWindowWidth() - 360.0f);
    ImGui::SameLine(right_x);
    ImGui::TextColored(ImVec4(0.25f, 0.95f, 0.55f, 1.0f), "FPS: %.1f", telemetry.disp_fps);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.25f, 1.0f), "| AI: %.1fms", telemetry.ai_ms);
    ImGui::SameLine();
    ImGui::TextColored(telemetry.is_int8 ? ImVec4(0.30f, 0.90f, 1.0f, 1.0f) : ImVec4(0.85f, 0.65f, 1.0f, 1.0f),
                       "| %s | DB:%d", telemetry.is_int8 ? "INT8" : "FP32", enrolled_count);

    ImGui::EndChild();
}

void NativeDashboardUI::renderLiveMonitorTab(const std::vector<TrackedFace>& active_faces,
                                             const TelemetrySnapshot& telemetry,
                                             FaceDatabase& db) {
    (void)db;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float left_w = avail.x * 0.68f;

    // Left: Live Zero-Copy Video Feed
    ImGui::BeginChild("VideoPane", ImVec2(left_w, avail.y), true);
    ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f),
                       "LIVE CAMERA STREAM (%dx%d)  |  Cam: %.1f FPS  |  Display: %.1f FPS  |  Optical Flow 60Hz",
                       telemetry.frame_w, telemetry.frame_h, telemetry.cam_fps, telemetry.disp_fps);
    ImGui::Separator();
    const ImVec2 vid_avail = ImGui::GetContentRegionAvail();
    drawVideoPanel(vid_avail.x, vid_avail.y);
    ImGui::EndChild();

    ImGui::SameLine();

    // Right: Active Detection Card + Recent Recognition Logs
    ImGui::BeginChild("RightSidebar", ImVec2(0, avail.y), true);

    ImGui::TextColored(ImVec4(0.20f, 0.88f, 1.00f, 1.0f), "ACTIVE DETECTION");
    ImGui::Separator();

    if (!active_faces.empty()) {
        const auto& best = active_faces[0];
        const bool is_known = (best.match.status == "KNOWN");
        ImGui::TextColored(is_known ? ImVec4(0.20f, 0.95f, 0.50f, 1.0f) : ImVec4(1.0f, 0.65f, 0.20f, 1.0f),
                           "Status: %s", best.match.status.c_str());
        ImGui::Text("Name:       %s", best.match.name.c_str());
        ImGui::Text("Student ID: %s", best.match.student_id.empty() ? "-" : best.match.student_id.c_str());
        ImGui::Text("Role:       %s", best.match.role.empty() ? "-" : best.match.role.c_str());

        char sim_overlay[32];
        std::snprintf(sim_overlay, sizeof(sim_overlay), "Similarity: %.1f%%", best.match.similarity * 100.0f);
        ImGui::ProgressBar(std::clamp(best.match.similarity, 0.0f, 1.0f), ImVec2(-1, 20), sim_overlay);
        ImGui::TextDisabled("Faces in frame: %zu | Detect: %.1fms", active_faces.size(), best.detect_ms);
    } else {
        ImGui::TextDisabled("No face currently detected in camera view...");
        ImGui::Dummy(ImVec2(0, 68));
    }

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.20f, 0.88f, 1.00f, 1.0f), "RECENT RECOGNITION LOGS");
    ImGui::Separator();

    ImGui::BeginChild("LogsScroll", ImVec2(0, 0), false);
    for (const auto& log : logs_cache_) {
        const bool known = (log.status == "KNOWN");
        ImGui::TextColored(known ? ImVec4(0.25f, 0.92f, 0.55f, 1.0f) : ImVec4(0.95f, 0.65f, 0.25f, 1.0f),
                           "[%s] %s (%.0f%%)",
                           log.status.c_str(), log.matched_name.c_str(), log.similarity * 100.0f);
        ImGui::TextDisabled("  %s | %.1f ms", log.timestamp.c_str(), log.total_latency_ms);
        ImGui::Separator();
    }
    if (logs_cache_.empty()) {
        ImGui::TextDisabled("No recognition logs recorded yet.");
    }
    ImGui::EndChild();

    ImGui::EndChild();
}

void NativeDashboardUI::renderRegisterTab(const std::vector<TrackedFace>& active_faces,
                                          const DashboardCallbacks& callbacks) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float left_w = avail.x * 0.58f;

    // Left: Live Preview for Enrollment
    ImGui::BeginChild("EnrollPreview", ImVec2(left_w, avail.y), true);
    ImGui::TextColored(ImVec4(0.20f, 0.88f, 1.00f, 1.0f), "LIVE FACE CAPTURE PREVIEW");
    ImGui::SameLine();
    if (active_faces.size() == 1) {
        ImGui::TextColored(ImVec4(0.20f, 0.95f, 0.50f, 1.0f), "[READY: 1 Face Locked]");
    } else if (active_faces.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.70f, 0.20f, 1.0f), "[Look directly at camera]");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.40f, 0.30f, 1.0f), "[Multiple faces detected - keep 1 person]");
    }
    ImGui::Separator();
    const ImVec2 vid_avail = ImGui::GetContentRegionAvail();
    drawVideoPanel(vid_avail.x, vid_avail.y);
    ImGui::EndChild();

    ImGui::SameLine();

    // Right: Registration Form
    ImGui::BeginChild("EnrollForm", ImVec2(0, avail.y), true);
    ImGui::TextColored(ImVec4(0.20f, 0.88f, 1.00f, 1.0f), "REGISTER NEW FACE TO SQLITE");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Full Name (Required):");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##EnrollName", enroll_name_, sizeof(enroll_name_));

    ImGui::Spacing();
    ImGui::Text("Student / Employee ID:");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##EnrollSID", enroll_sid_, sizeof(enroll_sid_));

    ImGui::Spacing();
    ImGui::Text("Role:");
    static const char* kRoles[] = {"Student", "Teacher", "Staff", "Admin", "Guest"};
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##EnrollRole", &enroll_role_idx_, kRoles, IM_ARRAYSIZE(kRoles));

    ImGui::Spacing();
    ImGui::Spacing();

    const bool can_submit = (enroll_name_[0] != '\0');
    if (!can_submit) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.62f, 0.38f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.12f, 0.75f, 0.46f, 1.0f));
    if (ImGui::Button("Capture & Enroll Face Now", ImVec2(-1, 42))) {
        if (callbacks.on_enroll) {
            std::string msg;
            enroll_status_ok_ = callbacks.on_enroll(enroll_name_, enroll_sid_, kRoles[enroll_role_idx_], msg);
            enroll_status_msg_ = msg;
            if (enroll_status_ok_) {
                force_db_refresh_ = true;
            }
        }
    }
    ImGui::PopStyleColor(2);
    if (!can_submit) ImGui::EndDisabled();

    if (!enroll_status_msg_.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", enroll_status_msg_.c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("Tip: Capture 2-3 angles per person (front, slight left, slight right)");
    ImGui::TextDisabled("with the same Name to improve recognition accuracy.");
    ImGui::EndChild();
}

void NativeDashboardUI::renderPersonsTab(FaceDatabase& db) {
    ImGui::BeginChild("PersonsPane", ImVec2(0, 0), true);
    ImGui::TextColored(ImVec4(0.20f, 0.88f, 1.00f, 1.0f),
                       "REGISTERED PERSONS DATABASE (%zu persons | %d vectors)",
                       persons_cache_.size(), db.embeddingCount());

    ImGui::SameLine(ImGui::GetWindowWidth() - 340.0f);
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint("##SearchPerson", "Search name or ID...", search_buf_, sizeof(search_buf_));
    ImGui::SameLine();
    if (ImGui::Button("Refresh", ImVec2(90, 0))) {
        db.reloadCache();
        force_db_refresh_ = true;
    }

    ImGui::Separator();
    ImGui::Spacing();

    const std::string filter = toLowerStr(search_buf_);
    int delete_id = -1;

    if (ImGui::BeginTable("PersonsTable", 7,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Student ID", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("Role", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Vectors", ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableSetupColumn("Created At", ImGuiTableColumnFlags_WidthFixed, 165.0f);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 85.0f);
        ImGui::TableHeadersRow();

        for (const auto& p : persons_cache_) {
            if (!filter.empty()) {
                if (toLowerStr(p.name).find(filter) == std::string::npos &&
                    toLowerStr(p.student_id).find(filter) == std::string::npos) {
                    continue;
                }
            }

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("%d", p.id);
            ImGui::TableSetColumnIndex(1); ImGui::Text("%s", p.name.c_str());
            ImGui::TableSetColumnIndex(2); ImGui::Text("%s", p.student_id.empty() ? "-" : p.student_id.c_str());
            ImGui::TableSetColumnIndex(3); ImGui::Text("%s", p.role.c_str());
            ImGui::TableSetColumnIndex(4); ImGui::Text("%d", p.embedding_count);
            ImGui::TableSetColumnIndex(5); ImGui::TextDisabled("%s", p.created_at.c_str());
            ImGui::TableSetColumnIndex(6);
            ImGui::PushID(p.id);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.18f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.25f, 0.28f, 1.0f));
            if (ImGui::SmallButton("Delete")) {
                delete_id = p.id;
            }
            ImGui::PopStyleColor(2);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (delete_id >= 0) {
        db.deletePerson(delete_id);
        force_db_refresh_ = true;
    }

    ImGui::EndChild();
}

void NativeDashboardUI::renderSettingsTab(const TelemetrySnapshot& telemetry,
                                          AppConfig& cfg,
                                          FaceDatabase& db,
                                          const DashboardCallbacks& callbacks) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float left_w = avail.x * 0.50f;

    // Left: Live Runtime Settings
    ImGui::BeginChild("SettingsLeft", ImVec2(left_w, avail.y), true);
    ImGui::TextColored(ImVec4(0.20f, 0.88f, 1.00f, 1.0f), "RUNTIME PARAMETERS");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Cosine Similarity Threshold:");
    ImGui::SliderFloat("##ThrSlider", &cfg.threshold, 0.35f, 0.90f, "%.2f");
    ImGui::TextDisabled("Higher = stricter match (default 0.60)");

    ImGui::Spacing();
    ImGui::Text("YuNet Detection Scale Width (px):");
    ImGui::SliderInt("##DetWidth", &cfg.detect_width, 200, 640, "%d px");
    ImGui::TextDisabled("Lower = faster AI on Pi 3 (256px) | Higher = farther faces (400-640px)");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.20f, 0.88f, 1.00f, 1.0f), "AI INTERVAL & BOX PREDICTION");

    ImGui::Text("Run AI Detection Every N Frames:");
    ImGui::SliderInt("##DetectInterval", &cfg.detect_every_n_frames, 1, 30, "Every %d frame(s)");
    ImGui::TextDisabled("1 = every frame | 5-15 = lower CPU load (great with Prediction ON)");

    ImGui::Spacing();
    ImGui::Checkbox("Enable Box Prediction (Optical Flow 60 FPS Lock)", &cfg.enable_predict);
    ImGui::TextDisabled("ON = smooth 60FPS AR face lock | OFF = raw AI boxes only");

    ImGui::Spacing();
    ImGui::Text("Predict / Box Hold Duration (sec):");
    ImGui::SliderFloat("##PredictHoldSec", &cfg.predict_hold_sec, 0.10f, 5.00f, "%.2f sec");
    ImGui::TextDisabled("How long box stays locked/predicted between AI detections");

    ImGui::Spacing();
    ImGui::Text("Re-Verify Identity Interval (sec):");
    ImGui::SliderFloat("##RecIntervalSec", &cfg.rec_interval_sec, 0.20f, 10.00f, "%.2f sec");
    ImGui::TextDisabled("How often MobileFaceNet re-checks a known person's identity");

    ImGui::Spacing();
    ImGui::Checkbox("Mirror Horizontal Flip", &cfg.flip_horizontal);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Camera Resolution:");
    static const char* kResLabels[] = {
        "320 x 240  (QVGA - Pi 3 Ultra Fast)",
        "480 x 360  (Balanced Low)",
        "640 x 480  (VGA - Recommended)",
        "800 x 600  (SVGA)",
        "1280 x 720 (720p HD)",
        "1920 x 1080 (1080p Full HD)"
    };
    static const int kResW[] = {320, 480, 640, 800, 1280, 1920};
    static const int kResH[] = {240, 360, 480, 600, 720,  1080};

    if (!res_idx_initialized_) {
        for (int i = 0; i < IM_ARRAYSIZE(kResW); ++i) {
            if (cfg.width == kResW[i] && cfg.height == kResH[i]) {
                selected_res_idx_ = i;
                break;
            }
        }
        pending_cam_id_ = cfg.camera_id;
        res_idx_initialized_ = true;
    }

    ImGui::SetNextItemWidth(260.0f);
    ImGui::Combo("##ResCombo", &selected_res_idx_, kResLabels, IM_ARRAYSIZE(kResLabels));
    ImGui::SameLine();
    if (ImGui::Button("Apply Resolution", ImVec2(140, 0))) {
        if (callbacks.on_change_resolution) {
            callbacks.on_change_resolution(kResW[selected_res_idx_], kResH[selected_res_idx_], settings_status_msg_);
        }
    }

    ImGui::Spacing();
    ImGui::Text("Switch Camera Device ID:");
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputInt("##CamID", &pending_cam_id_);
    pending_cam_id_ = std::max(0, pending_cam_id_);
    ImGui::SameLine();
    if (ImGui::Button("Apply Camera", ImVec2(130, 0))) {
        if (callbacks.on_switch_camera) {
            callbacks.on_switch_camera(pending_cam_id_, settings_status_msg_);
        }
    }
    if (!settings_status_msg_.empty()) {
        ImGui::TextColored(ImVec4(0.30f, 0.90f, 1.0f, 1.0f), "%s", settings_status_msg_.c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Reload SQLite Cache", ImVec2(180, 32))) {
        db.reloadCache();
        force_db_refresh_ = true;
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.22f, 0.22f, 1.0f));
    if (ImGui::Button("Clear Recognition Logs", ImVec2(190, 32))) {
        db.clearLogs();
        force_db_refresh_ = true;
    }
    ImGui::PopStyleColor();
    ImGui::EndChild();

    ImGui::SameLine();

    // Right: Real-Time Telemetry & Performance Graphs
    ImGui::BeginChild("SettingsRight", ImVec2(0, avail.y), true);
    ImGui::TextColored(ImVec4(0.20f, 0.88f, 1.00f, 1.0f), "SYSTEM TELEMETRY & GRAPHS");
    ImGui::Separator();
    ImGui::Text("Backend: %s", telemetry.backend_name.c_str());
    ImGui::Text("Model:   %s", telemetry.model_name.c_str());
    ImGui::Text("Camera:  %dx%d (%.1f FPS)", telemetry.frame_w, telemetry.frame_h, telemetry.cam_fps);
    ImGui::Text("Display: %.1f FPS | AI Worker: %.1f ms", telemetry.disp_fps, telemetry.ai_ms);
    ImGui::Spacing();

    ImGui::Text("Display FPS (Real-time):");
    ImGui::PlotLines("##FPSPlot", fps_history_.data(), static_cast<int>(fps_history_.size()),
                     0, nullptr, 0.0f, 65.0f, ImVec2(-1, 100));

    ImGui::Spacing();
    ImGui::Text("AI Pipeline Latency (ms):");
    ImGui::PlotLines("##AIPlot", ai_ms_history_.data(), static_cast<int>(ai_ms_history_.size()),
                     0, nullptr, 0.0f, 120.0f, ImVec2(-1, 100));

    ImGui::EndChild();
}

void NativeDashboardUI::renderFrame(const cv::Mat& display_bgr,
                                    const std::vector<TrackedFace>& active_faces,
                                    const TelemetrySnapshot& telemetry,
                                    AppConfig& cfg,
                                    FaceDatabase& db,
                                    const DashboardCallbacks& callbacks) {
    if (!window_) return;

    glfwPollEvents();
    updateVideoTexture(display_bgr);

    // Update graph history
    fps_history_.erase(fps_history_.begin());
    fps_history_.push_back(static_cast<float>(telemetry.disp_fps));
    ai_ms_history_.erase(ai_ms_history_.begin());
    ai_ms_history_.push_back(static_cast<float>(telemetry.ai_ms));

    // Refresh SQLite UI lists once per second or on user action
    const double now_sec = glfwGetTime();
    if (force_db_refresh_ || (now_sec - last_db_refresh_sec_) > 1.5) {
        logs_cache_ = db.getRecentLogs(25);
        if (active_tab_ == 2 || force_db_refresh_) {
            persons_cache_ = db.listPersons();
        }
        last_db_refresh_sec_ = now_sec;
        force_db_refresh_ = false;
    }

    ImGui_ImplOpenGL2_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);

    constexpr ImGuiWindowFlags root_flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("RootDashboard", nullptr, root_flags);
    renderTopBar(telemetry, db.enrolledPersonCount());

    switch (active_tab_) {
        case 0: renderLiveMonitorTab(active_faces, telemetry, db); break;
        case 1: renderRegisterTab(active_faces, callbacks); break;
        case 2: renderPersonsTab(db); break;
        case 3: renderSettingsTab(telemetry, cfg, db, callbacks); break;
        default: break;
    }

    ImGui::End();

    ImGui::Render();
    int disp_w = 0, disp_h = 0;
    glfwGetFramebufferSize(window_, &disp_w, &disp_h);
    glViewport(0, 0, disp_w, disp_h);
    glClearColor(0.05f, 0.08f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window_);
}

} // namespace fr
