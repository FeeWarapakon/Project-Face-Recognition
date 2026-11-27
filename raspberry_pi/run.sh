#!/usr/bin/env bash
# ==============================================================================
# โครงงาน: Face Recognition on Raspberry Pi 5 / 4
# สคริปต์: run.sh (ตัวช่วยรันโปรแกรมแบบเลือก 1, 2, 3, 4, 5)
# ==============================================================================

PI_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$PI_DIR/.." && pwd)"
cd "$ROOT_DIR"

# ตรวจสอบและเปิดใช้งาน Python Virtual Environment
VENV_ACTIVATED=0
PYTHON_BIN="python3"

if [ -f "$ROOT_DIR/Antigravity/venv/bin/activate" ]; then
    source "$ROOT_DIR/Antigravity/venv/bin/activate"
    PYTHON_BIN="$ROOT_DIR/Antigravity/venv/bin/python3"
    VENV_ACTIVATED=1
    VENV_PATH="$ROOT_DIR/Antigravity/venv"
elif [ -f "$ROOT_DIR/venv/bin/activate" ]; then
    source "$ROOT_DIR/venv/bin/activate"
    PYTHON_BIN="$ROOT_DIR/venv/bin/python3"
    VENV_ACTIVATED=1
    VENV_PATH="$ROOT_DIR/venv"
elif [ -f "$ROOT_DIR/.venv/bin/activate" ]; then
    source "$ROOT_DIR/.venv/bin/activate"
    PYTHON_BIN="$ROOT_DIR/.venv/bin/python3"
    VENV_ACTIVATED=1
    VENV_PATH="$ROOT_DIR/.venv"
elif [ -f "$PI_DIR/venv/bin/activate" ]; then
    source "$PI_DIR/venv/bin/activate"
    PYTHON_BIN="$PI_DIR/venv/bin/python3"
    VENV_ACTIVATED=1
    VENV_PATH="$PI_DIR/venv"
fi

# ตรวจสอบแพ็กเกจ Flask & Flask-CORS สำหรับ Web Streaming Server
ensure_python_deps() {
    "$PYTHON_BIN" -c "import flask, flask_cors" 2>/dev/null
    if [ $? -ne 0 ]; then
        echo "[*] กำลังติดตั้งแพ็กเกจที่จำเป็นสำหรับ Web Server (flask, flask-cors)..."
        pip install flask flask-cors 2>/dev/null || "$PYTHON_BIN" -m pip install flask flask-cors 2>/dev/null || pip3 install flask flask-cors --break-system-packages
    fi
}

while true; do
    clear
    echo "========================================================"
    echo "   🍓 RASPBERRY PI 5 // FACE RECOGNITION RUNNER         "
    echo "========================================================"
    if [ $VENV_ACTIVATED -eq 1 ]; then
        echo "   [+] Python VENV: เปิดใช้งาน ($VENV_PATH)"
    else
        echo "   [!] Python VENV: ระบบปกติ (System Python)"
    fi
    echo "--------------------------------------------------------"
    echo ""
    echo "   [1] รัน 1_quantize_model.py     (แปลงโมเดลเป็น INT8)"
    echo "   [2] รัน 2_enroll_faces.py       (บันทึกใบหน้าบุคคล)"
    echo "   [3] รัน 3_realtime_recognition.py (AI Engine บน Port 5000)"
    echo "   [4] รันเว็บ npm run dev         (Dashboard บน Port 5173)"
    echo "   [5] รันทั้งคู่พร้อมกัน          (AI Engine + Web Dashboard)"
    echo "   [6] Build & รัน C++ Native     (เร็วสุด! Zero-Copy สำหรับ Pi 3 / Pi 5)"
    echo "   [0] ออกจากโปรแกรม (Exit)"
    echo ""
    echo "========================================================"
    read -p "กรุณาเลือกหมายเลข [1-6 หรือ 0]: " choice

    case $choice in
        1)
            echo ""
            echo "[*] กำลังรัน 1_quantize_model.py..."
            cd "$ROOT_DIR/Antigravity"
            "$PYTHON_BIN" 1_quantize_model.py
            read -p "กด Enter เพื่อกลับสู่เมนูหลัก..."
            ;;
        2)
            echo ""
            echo "[*] กำลังรัน 2_enroll_faces.py..."
            cd "$ROOT_DIR/Antigravity"
            "$PYTHON_BIN" 2_enroll_faces.py
            read -p "กด Enter เพื่อกลับสู่เมนูหลัก..."
            ;;
        3)
            echo ""
            echo "[*] ตรวจสอบแพ็กเกจ Python..."
            ensure_python_deps
            echo "[*] กำลังรัน 3_realtime_recognition.py (Port 5000)..."
            fuser -k 5000/tcp 2>/dev/null
            cd "$ROOT_DIR/Antigravity"
            "$PYTHON_BIN" 3_realtime_recognition.py --host 0.0.0.0 --port 5000
            echo ""
            read -p "กด Enter เพื่อกลับสู่เมนูหลัก..."
            ;;
        4)
            echo ""
            echo "[*] กำลังรัน Frontend Dashboard (npm run dev)..."
            fuser -k 5173/tcp 2>/dev/null
            cd "$ROOT_DIR/client/face-recognition"
            if [ ! -d "node_modules" ]; then
                echo "[*] กำลังติดตั้งแพ็กเกจ npm..."
                npm install
            fi
            npm run dev -- --host 0.0.0.0 --port 5173 --open
            echo ""
            read -p "กด Enter เพื่อกลับสู่เมนูหลัก..."
            ;;
        5)
            echo ""
            echo "[*] เริ่มต้นระบบแบบ Full-Stack (AI Backend + Web Frontend)..."
            ensure_python_deps
            
            # เคลียร์พอร์ตเก่าก่อนเริ่ม
            fuser -k 5000/tcp 2>/dev/null
            fuser -k 5173/tcp 2>/dev/null
            sleep 0.5

            # 1. รัน AI Backend ใน Background
            echo "[1/2] กำลังเปิด AI Backend Engine บน Port 5000..."
            cd "$ROOT_DIR/Antigravity"
            "$PYTHON_BIN" 3_realtime_recognition.py --host 0.0.0.0 --port 5000 &
            PY_PID=$!

            # รอให้ Port 5000 เปิดใช้งาน
            sleep 2

            # 2. รัน Frontend Web Server
            echo "[2/2] กำลังเปิด Web Dashboard บน Port 5173..."
            cd "$ROOT_DIR/client/face-recognition"
            if [ ! -d "node_modules" ]; then
                npm install
            fi

            # ฟังก์ชันดักจับ Ctrl+C เพื่อปิดทั้งสอง Process
            cleanup_dual() {
                echo ""
                echo "[!] กำลังปิดทั้งระบบ (AI Engine & Web Dashboard)..."
                kill $PY_PID 2>/dev/null
                fuser -k 5000/tcp 2>/dev/null
                fuser -k 5173/tcp 2>/dev/null
                trap - SIGINT SIGTERM
            }
            trap cleanup_dual SIGINT SIGTERM

            npm run dev -- --host 0.0.0.0 --port 5173 --open

            cleanup_dual
            read -p "กด Enter เพื่อกลับสู่เมนูหลัก..."
            ;;
        6)
            echo ""
            echo "[*] กำลังตรวจสอบ/คอมไพล์ C++ Native Engine..."
            cd "$ROOT_DIR/cpp"
            if [ ! -f "build/face_recognition_native" ]; then
                chmod +x build_pi.sh
                ./build_pi.sh
            fi
            TOTAL_MEM_MB=$(free -m | awk '/^Mem:/{print $2}')
            if [ "$TOTAL_MEM_MB" -lt 1500 ]; then
                echo "[*] ตรวจพบบอร์ด RAM ต่ำ (${TOTAL_MEM_MB} MB, เช่น Pi 3) -> เปิดโหมด --pi3"
                ./build/face_recognition_native --pi3
            else
                ./build/face_recognition_native
            fi
            read -p "กด Enter เพื่อกลับสู่เมนูหลัก..."
            ;;
        0)
            echo "ออกจากโปรแกรมเรียบร้อยแล้ว"
            fuser -k 5000/tcp 2>/dev/null
            fuser -k 5173/tcp 2>/dev/null
            exit 0
            ;;
        *)
            echo "[!] เลือกไม่ถูกต้อง กรุณาเลือก 1-6 หรือ 0"
            sleep 1
            ;;
    esac
done
