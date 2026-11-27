#!/usr/bin/env bash
# ==============================================================================
# Build Script for Lightweight Native C++ Face Recognition
# Supported: Raspberry Pi 3 / Pi 4 / Pi 5 / Orange Pi / Jetson / Ubuntu / Debian
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "=================================================================="
echo "  Building Lightweight Native C++ Face Recognition Engine"
echo "=================================================================="

# 1. Install system dependencies if missing
if ! dpkg -s libopencv-dev libsqlite3-dev cmake build-essential >/dev/null 2>&1; then
    echo "[*] Installing required C++ dependencies (OpenCV, SQLite3, CMake)..."
    sudo apt-get update
    sudo apt-get install -y build-essential cmake libopencv-dev libsqlite3-dev
fi

# 2. Auto-detect ONNX Runtime C++ SDK if installed in /usr/local or ./onnxruntime
ORT_FLAGS=""
if [ -d "/usr/local/include/onnxruntime" ] || [ -f "/usr/local/include/onnxruntime_cxx_api.h" ]; then
    echo "[+] Detected system ONNX Runtime C++ -> Enabling INT8 acceleration!"
    ORT_FLAGS="-DUSE_ONNXRUNTIME=ON"
elif [ -d "$SCRIPT_DIR/onnxruntime" ]; then
    echo "[+] Detected local ONNX Runtime C++ -> Enabling INT8 acceleration!"
    ORT_FLAGS="-DUSE_ONNXRUNTIME=ON -DONNXRUNTIME_ROOT=$SCRIPT_DIR/onnxruntime"
else
    echo "[i] Building with OpenCV DNN backend (zero extra SDK needed)."
    echo "    (Optional: Place ONNX Runtime C++ SDK in cpp/onnxruntime/ for INT8 QDQ acceleration)"
fi

# 3. Configure & Compile (use nproc - 1 on Pi 3 to avoid RAM pressure)
JOBS=$(nproc)
TOTAL_MEM_MB=$(free -m | awk '/^Mem:/{print $2}')
if [ "$TOTAL_MEM_MB" -lt 1500 ]; then
    echo "[i] Low-RAM SBC detected (${TOTAL_MEM_MB} MB, e.g. Pi 3) -> Limiting parallel compile jobs to 2."
    JOBS=2
fi

cmake -B build -DCMAKE_BUILD_TYPE=Release $ORT_FLAGS
cmake --build build -j "$JOBS"

echo ""
echo "=================================================================="
echo "  [OK] Build Successful!"
echo "  Run on Pi 5 / PC : ./build/face_recognition_native"
echo "  Run on Pi 3      : ./build/face_recognition_native --pi3"
echo "=================================================================="
