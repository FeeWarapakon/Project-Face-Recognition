cmake_minimum_required(VERSION 3.16)
project(FaceRecognitionNative VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(USE_ONNXRUNTIME "Enable ONNX Runtime C++ for INT8 Quantized Inference" OFF)
set(ONNXRUNTIME_ROOT "" CACHE PATH "Path to ONNX Runtime C++ SDK directory (containing include/ and lib/)")

# Required Packages: OpenCV 4, SQLite3, OpenGL, GLFW3, Threads
find_package(OpenCV REQUIRED COMPONENTS core imgproc highgui videoio video dnn objdetect calib3d)
find_package(SQLite3 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLFW REQUIRED glfw3)
endif()

# Auto-fetch Dear ImGui (pure C++, ultra-lightweight ~2 MB, zero manual download)
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.6
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl2.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
if(glfw3_FOUND)
    target_link_libraries(imgui_lib PUBLIC glfw OpenGL::GL)
else()
    target_include_directories(imgui_lib PUBLIC ${GLFW_INCLUDE_DIRS})
    target_link_libraries(imgui_lib PUBLIC ${GLFW_LIBRARIES} OpenGL::GL)
endif()

add_executable(face_recognition_native
    src/main.cpp
    src/face_detector.cpp
    src/face_recognizer.cpp
    src/face_tracker.cpp
    src/database.cpp
    src/ui_dashboard.cpp
)

target_include_directories(face_recognition_native PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
    ${SQLite3_INCLUDE_DIRS}
)

target_link_libraries(face_recognition_native PRIVATE
    imgui_lib
    ${OpenCV_LIBS}
    SQLite::SQLite3
    Threads::Threads
)

# Optional ONNX Runtime C++ linkage (Recommended for INT8 mobilefacenet_int8.onnx)
if(USE_ONNXRUNTIME)
    find_path(ORT_INCLUDE_DIR onnxruntime_cxx_api.h
        HINTS "${ONNXRUNTIME_ROOT}/include" /usr/local/include/onnxruntime /usr/include/onnxruntime
    )
    find_library(ORT_LIBRARY onnxruntime
        HINTS "${ONNXRUNTIME_ROOT}/lib" /usr/local/lib /usr/lib
    )
    if(ORT_INCLUDE_DIR AND ORT_LIBRARY)
        message(STATUS "ONNX Runtime C++ enabled: ${ORT_LIBRARY}")
        target_compile_definitions(face_recognition_native PRIVATE USE_ONNXRUNTIME=1)
        target_include_directories(face_recognition_native PRIVATE ${ORT_INCLUDE_DIR})
        target_link_libraries(face_recognition_native PRIVATE ${ORT_LIBRARY})
    else()
        message(WARNING "USE_ONNXRUNTIME=ON requested, but ONNX Runtime SDK not found. Falling back to OpenCV DNN.")
    endif()
endif()

# Architecture-specific optimizations (ARM NEON / x86 AVX)
if(MSVC)
    target_compile_options(face_recognition_native PRIVATE /O2 /W3)
else()
    target_compile_options(face_recognition_native PRIVATE -O3 -Wall -Wextra)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|armv7l")
        target_compile_options(face_recognition_native PRIVATE -march=native -mtune=native)
    endif()
endif()
