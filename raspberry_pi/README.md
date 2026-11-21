# 🍓 คู่มือการใช้งานตัวรันบน Raspberry Pi

สคริปต์ `run.sh` เป็นตัวช่วยให้สามารถกดเลือกรันโปรแกรมแต่ละส่วนได้ง่ายๆ โดยไม่ต้องคอยจำหรือพิมพ์คำสั่งยาวๆ ใน Terminal

---

### 🚀 วิธีเปิดใช้งาน

1. เปิด Terminal ในโฟลเดอร์นี้ แล้วพิมพ์:
   ```bash
   chmod +x run.sh
   ./run.sh
   ```
   *(หรือดับเบิลคลิกที่ไฟล์ `run.sh` บนหน้าจอ Desktop ของ Raspberry Pi แล้วเลือก **Execute in Terminal**)*

---

### 📋 เมนูตัวเลือกการรัน:

* **`[1] รัน 1_quantize_model.py`**
  - แปลงโมเดล MobileFaceNet FP32 เป็น INT8 Quantization เพื่อเพิ่มความเร็วบนชิป Raspberry Pi

* **`[2] รัน 2_enroll_faces.py`**
  - โปรแกรมเปิดกล้องและบันทึกใบหน้าบุคคลใหม่ลงสู่ฐานข้อมูล SQLite

* **`[3] รัน 3_realtime_recognition.py`**
  - เริ่มต้นระบบ AI ตรวจจับและรู้จำใบหน้าแบบ Real-time พร้อมเปิด REST API / Streaming บน Port 5000 (ดู Log AI ใน Terminal นี้)

* **`[4] รันเว็บ npm run dev`**
  - เริ่มต้นหน้าเว็บ Dashboard (SvelteKit) บน Port 5173 และเปิดเบราว์เซอร์ให้อัตโนมัติ

* **`[5] รันทั้งคู่พร้อมกัน (AI Engine + Web Dashboard)`** ⭐ *(แนะนำ)*
  - เปิดทั้งระบบ AI Backend (Port 5000) และ Web Frontend (Port 5173) ในคำสั่งเดียว เหมาะสำหรับการใช้งานจริง

* **`[0] ออกจากโปรแกรม (Exit)`**

---

### 💡 การแก้ปัญหาพอร์ต 5000 หรือภาพกล้องไม่ขึ้น

1. **ภาพกล้องไม่ขึ้นบนหน้าเว็บ**:
   - ระบบรองรับทั้งกล้อง USB Webcam และกล้อง Raspberry Pi Camera
   - สามารถคลิกเปลี่ยนหมายเลขกล้องได้ทันทีที่แถบเมนูด้านบน (`CAM: ... ▼`) หรือในหน้า **Settings**
   - หากกล้องยังไม่พร้อม ระบบจะแสดงหน้าจอ Standby พร้อมบอกเวลาและสถานะ Port 5000 อัตโนมัติ

2. **การติดตั้งแพ็กเกจ Python ที่จำเป็น**:
   ```bash
   pip install flask flask-cors opencv-python onnxruntime
   ```
