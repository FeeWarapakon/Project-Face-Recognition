# ⚡ Lightweight Native C++ Face Recognition + ImGui Dashboard

เวอร์ชัน **C++17 Native Cross-Platform + Dear ImGui Dashboard** ที่ทดแทนการทำงานของหน้าเว็บได้ครบ 100% โดยยังคงความเบาสูงสุด (**Zero-Copy GPU Texture, RAM < 100 MB, 60 FPS Delay-Compensated Optical Flow**) รองรับตั้งแต่ **Raspberry Pi 3, Raspberry Pi 5, บอร์ด SBC ยี่ห้ออื่น (Orange Pi / Jetson)** ไปจนถึง **Windows (`.exe`)**

---

## 🖥️ ฟีเจอร์ครบทุกแท็บใน Native Dashboard UI
1. **Live Monitor:** ภาพสดจากกล้องบน GPU Texture พร้อมกรอบ **Delay-Compensated Optical Flow 60 FPS**, การ์ดสถานะบุคคลที่ตรวจพบล่าสุด และแถบประวัติการสแกนแบบเรียลไทม์
2. **Register Face:** ลงทะเบียนใบหน้าบุคคลใหม่ผ่านกล้องสด (กรอกชื่อ, รหัสนักศึกษา, บทบาท แล้วกดปุ่ม Capture บันทึกลง SQLite ทันที)
3. **Persons DB:** ตารางแสดงรายชื่อบุคคลทั้งหมดในระบบ พร้อมช่องค้นหา, จำนวนเวกเตอร์ใบหน้า, ปุ่ม Refresh และปุ่มลบรายชื่อ
4. **Settings & Telemetry:** สไลเดอร์ปรับค่า `Threshold`, ปรับความกว้างภาพตรวจจับ `YuNet`, สลับกล้อง (`Camera ID`), สลับ Mirror Flip พร้อม **กราฟ FPS และ AI Latency แบบเรียลไทม์**

---

## 🍓 วิธี Build & รันบน Raspberry Pi / Linux SBC (Orange Pi, Jetson, Ubuntu)

```bash
cd cpp
chmod +x build_pi.sh
./build_pi.sh
```
*(สคริปต์จะติดตั้ง `libopencv-dev`, `libsqlite3-dev`, `libglfw3-dev`, `libgl1-mesa-dev` และดึง Dear ImGui ผ่าน CMake ให้อัตโนมัติ)*

### สั่งรันโปรแกรม:
* **สำหรับ Raspberry Pi 5 / PC / Jetson:**
  ```bash
  ./build/face_recognition_native
  ```
* **สำหรับ Raspberry Pi 3 (โหมดประหยัดทรัพยากร 320x240, Detect 256px):**
  ```bash
  ./build/face_recognition_native --pi3
  ```

---

## 🪟 วิธี Build & รันบน Windows (Standalone `.exe`)

1. ติดตั้ง [CMake](https://cmake.org/), [Visual Studio Build Tools](https://visualstudio.microsoft.com/), และแพ็กเกจผ่าน `vcpkg`:
   ```powershell
   vcpkg install opencv4 sqlite3 glfw3
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
