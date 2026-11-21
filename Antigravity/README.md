# Face Recognition on Raspberry Pi 5 (MobileFaceNet + INT8 Quantization)

โครงงานวิศวกรรมคอมพิวเตอร์ ระดับปริญญาตรี
- **สมาชิกกลุ่ม:** นายนราวิชญ์ อนุวงศ์, นายสัญญา ลุงจาย, นายวราปกรณ์ พิมพ์วงษ์
- **อาจารย์ที่ปรึกษา:** ผศ.สมนึก สุระธง

---

## 🌐 หน้าเว็บนำเสนอโครงงาน & สื่อการเรียนรู้ (Interactive Dashboard)
เปิดไฟล์ [`index.html`](file:///f:/Project/Antigravity/index.html) ในเบราว์เซอร์เพื่อดู:
1. การสรุปกระบวนการทำงานตั้งแต่ศูนย์ (Pipeline 101)
2. เครื่องมือจำลองการคำนวณ Quantization FP32 -> INT8 และ Cosine Similarity
3. กราฟเปรียบเทียบ Benchmark (ความเร็ว, ขนาดไฟล์, ความแม่นยำ)
4. แนวทางการตอบคำถามกรรมการในการสอบโครงงาน

---

## 🚀 สรุปขั้นตอนการรันโปรแกรมบน Raspberry Pi 5

### 1. ติดตั้ง Dependencies
```bash
pip install -r requirements.txt
```

### 2. แปลงโมเดลเป็น INT8 Quantization
```bash
python 1_quantize_model.py
```

### 3. ลงทะเบียนใบหน้าสมาชิกเข้าสู่ฐานข้อมูล
```bash
python 2_enroll_faces.py
```

### 4. รันระบบสแกนใบหน้า Real-time ด้วยกล้อง
```bash
python 3_realtime_recognition.py
```

### 5. รันทดสอบเปรียบเทียบความเร็ว (Benchmark)
```bash
python 4_benchmark_eval.py
```
