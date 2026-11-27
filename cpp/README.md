# ⚡ Lightweight Native C++ Face Recognition Engine

เวอร์ชัน **C++17 Native Cross-Platform** สำหรับงาน **Lightweight Edge Image Processing** ออกแบบมาเพื่อให้ทำงานได้เร็วที่สุด กินแรมน้อยที่สุด (< 80 MB) และไร้ความหน่วง (Zero-Copy Rendering + Lucas-Kanade Optical Flow 60 FPS) รองรับตั้งแต่ **Raspberry Pi 3, Raspberry Pi 5, บอร์ด SBC ยี่ห้ออื่น (Orange Pi / Jetson)** ไปจนถึง **Windows (`.exe`)**

---

## ✨ จุดเด่นทางสถาปัตยกรรม (Architecture Highlights)
* **True Multi-Threaded Pipeline:** แยกเธรดดึงภาพกล้อง (V4L2/DirectShow), เธรด AI Worker (YuNet + MobileFaceNet), และเธรดแสดงผลออกจากกัน 100% โดยไม่ติดข้อจำกัด GIL ของ Python
* **Zero-Lag 60 FPS Tracking:** ใช้ Lucas-Kanade Sparse Optical Flow ติดตามใบหน้าในทุกเฟรมกล้อง (< 1.5 ms) ทำให้กรอบไหลติดใบหน้าเนียนตาเท่าความเร็วกล้อง
* **Zero-Copy Native Display:** แสดงผลตรงผ่าน OpenCV HighGUI ไม่ต้องบีบอัด JPEG ส่งผ่าน HTTP และไม่ต้องเปิดเบราว์เซอร์
* **100% SQLite Compatible:** ใช้ฐานข้อมูล `Antigravity/face_recognition.db` ร่วมกับฝั่ง Python/Web ได้ทันที

---

## 🍓 วิธี Build & รันบน Raspberry Pi / Linux SBC (Orange Pi, Jetson, Ubuntu)

```bash
cd cpp
chmod +x build_pi.sh
./build_pi.sh
```

### สั่งรันโปรแกรม:
* **สำหรับ Raspberry Pi 5 / PC / Jetson:**
  ```bash
  ./build/face_recognition_native
  ```
* **สำหรับ Raspberry Pi 3 (โหมดประหยัดทรัพยากร 320x240, Detect 256px):**
  ```bash
  ./build/face_recognition_native --pi3
  ```

### (ทางเลือก) เปิดใช้งาน ONNX Runtime C++ สำหรับโมเดล INT8:
ดาวน์โหลด ONNX Runtime C++ (`linux-aarch64` หรือ `linux-arm`) จาก [ONNX Runtime Releases](https://github.com/microsoft/onnxruntime/releases) แตกไฟล์วางที่ `cpp/onnxruntime/` แล้วรัน `./build_pi.sh` ระบบจะตรวจจับและเปิดโหมด **INT8 QDQ** ให้อัตโนมัติ

---

## 🪟 วิธี Build & รันบน Windows (Standalone `.exe`)

1. ติดตั้ง [CMake](https://cmake.org/), [Visual Studio Build Tools](https://visualstudio.microsoft.com/), และแพ็กเกจผ่าน `vcpkg`:
   ```powershell
   vcpkg install opencv4 sqlite3
   ```
2. สั่ง Build ด้วย CMake:
   ```powershell
   cd cpp
   cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
   cmake --build build --config Release
   ```
3. รันไฟล์ `.exe`:
   ```powershell
   .\build\Release\face_recognition_native.exe
   ```

---

## ⌨️ คีย์ลัดขณะใช้งาน
* **`q` หรือ `ESC`**: ออกจากโปรแกรม
* **`r`**: โหลดข้อมูลใบหน้าจาก SQLite Database ใหม่ (Reload Cache)
* **`f`**: สลับโหมดกลับภาพกระจก (Mirror Flip)
