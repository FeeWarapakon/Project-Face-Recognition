# 🎯 Lightweight Real-Time Face Recognition on Edge Devices
### (MobileFaceNet INT8 Quantization + Lucas-Kanade Optical Flow + Native C++ & Web Dashboard)

โครงงานวิศวกรรมคอมพิวเตอร์ ระดับปริญญาตรี
* **สมาชิกผู้พัฒนา:** นายนราวิชญ์ อนุวงศ์, นายสัญญา ลุงจาย, นายวราปกรณ์ พิมพ์วงษ์
* **อาจารย์ที่ปรึกษา:** ผศ.สมนึก สุระธง

---

## 📌 ภาพรวมโครงงาน (Project Overview)
ระบบตรวจจับและรู้จำใบหน้าแบบเรียลไทม์ที่ออกแบบมาโดยเฉพาะสำหรับอุปกรณ์ **Edge AI / Single Board Computer (SBC)** ตั้งแต่ **Raspberry Pi 3, Raspberry Pi 5, Orange Pi, NVIDIA Jetson** ไปจนถึง **Windows PC** โดยเน้นการประมวลผลภาพน้ำหนักเบา (Lightweight Image Processing) ด้วย 3 แกนหลัก:

1. **Post-Training Static INT8 Quantization (Opset 13 QDQ):** บีบอัดโมเดล MobileFaceNet จาก 3.81 MB เหลือเพียง **1.20 MB (-68.6%)** โดยคงความสอดคล้องของเวกเตอร์ใบหน้าถึง **98.58%**
2. **Zero-Lag 60 FPS Tracking:** ผสาน Deep Learning Detector (**YuNet**) เข้ากับ **Lucas-Kanade Sparse Optical Flow** ทำให้กรอบสี่เหลี่ยมและจุด Landmark ไหลติดใบหน้าทุกเฟรมตามความเร็วกล้อง (< 1.5 ms)
3. **Dual Runtime Architecture:** รองรับทั้ง **C++17 Native Engine** (Zero-Copy, RAM < 80 MB, เหมาะสำหรับ Pi 3 – Pi 5 และ Windows `.exe`) และ **Python + SvelteKit Web Dashboard** (สำหรับควบคุมและมอนิเตอร์ผ่านเครือข่าย)

---

## 📂 โครงสร้างโปรเจกต์ (Repository Structure)

```text
Project-Face-Recognition/
├── cpp/                         # ⚡ Native C++17 Cross-Platform Engine (Pi 3 / Pi 5 / SBC / Windows)
│   ├── CMakeLists.txt           # สคริปต์ Build ข้ามแพลตฟอร์ม
│   ├── build_pi.sh              # สคริปต์ติดตั้งและคอมไพล์อัตโนมัติบน Linux/Pi
│   ├── include/                 # Header files (Detector, Recognizer, Tracker, Database)
│   └── src/                     # Source code (True Multi-Threaded Pipeline)
├── Antigravity/                 # 🐍 Python AI Pipeline, INT8 Quantization & Flask REST/MJPEG API
│   ├── 1_quantize_model.py      # แปลงโมเดล FP32 -> Static INT8 (Opset 13 QDQ)
│   ├── 2_enroll_faces.py        # ลงทะเบียนใบหน้าบุคคลลง SQLite
│   ├── 3_realtime_recognition.py# ระบบสแกน Real-time + Optical Flow + On-Demand MJPEG Server
│   ├── 4_benchmark_eval.py      # ทดสอบวัดผล Latency, FPS และขนาดโมเดล
│   ├── face_engine.py           # Core Engine (YuNet + MobileFaceNet INT8 Priority)
│   ├── anti_spoof.py            # MiniFASNet Liveness / Anti-Spoofing
│   ├── database.py              # SQLite Manager + In-Memory Cosine Cache
│   └── face_recognition.db      # ฐานข้อมูลใบหน้า (ใช้ร่วมกันทั้ง C++ และ Python)
├── client/face-recognition/     # 🌐 Modern Web Dashboard (SvelteKit 2 + Svelte 5 + TailwindCSS v4)
├── raspberry_pi/                # 🍓 Interactive Launcher Menu สำหรับ Raspberry Pi
└── run.sh                       # Entrypoint หลักสำหรับรันระบบทั้งหมด
```

---

## 🚀 วิธีเริ่มต้นใช้งาน (Quick Start)

### วิธีที่ 1: รันผ่านเมนูอัตโนมัติบน Raspberry Pi / Linux (แนะนำ ⭐)
```bash
chmod +x run.sh
./run.sh
```
* เลือก **`[5]`** เพื่อรัน **Python AI Backend (Port 5000) + SvelteKit Web Dashboard (Port 5173)** พร้อมกัน
* เลือก **`[6]`** เพื่อ Build และรัน **C++ Native Engine** (เร็วสุด กินแรมน้อยสุด รองรับ Pi 3 อัตโนมัติ)

### วิธีที่ 2: Build & รัน C++ Native Engine โดยตรง
ดูคู่มือละเอียดทั้ง **Raspberry Pi 3/5, SBC อื่นๆ และ Windows (`.exe`)** ได้ที่ [`cpp/README.md`](cpp/README.md)

```bash
cd cpp
./build_pi.sh
./build/face_recognition_native        # สำหรับ Pi 5 / PC
./build/face_recognition_native --pi3  # สำหรับ Pi 3 (320x240, Detect 256px)
```

---

## 📊 ผลลัพธ์ประสิทธิภาพ (Benchmark Summary)

| ตัวชี้วัด (Metric) | MobileFaceNet FP32 | MobileFaceNet INT8 (ปัจจุบัน) | ผลลัพธ์ |
| :--- | :---: | :---: | :---: |
| **ขนาดไฟล์โมเดล** | 3.81 MB | **1.20 MB** | 📉 เล็กลง **68.6%** |
| **Embedding Similarity (FP32 vs INT8)** | 100% | **98.58%** | 🎯 ความแม่นยำคงเดิม |
| **Feature Extraction Latency (Pi 5)** | ~35–45 ms | **15.66 ms** | ⚡ เร็วขึ้น ~2.5x–3x |
| **SQLite Vector Matching** | 0.08 ms | **0.08 ms** | 🔍 ค้นหาระดับไมโครวินาที |
| **Tracking Frame Rate (Optical Flow)** | 11–15 FPS | **30–60 FPS (Zero-Lag)** | 🎥 กรอบไหลติดหน้าทันที |
