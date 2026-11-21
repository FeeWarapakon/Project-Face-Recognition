# my_face_project/main.py
# ==============================================================================
# โครงงาน: Face Recognition on Raspberry Pi 5 (MobileFaceNet + INT8 Quantization)
# หน้าที่: ระบบสแกนและรู้จำใบหน้า Real-time ด้วย MTCNN + MobileFaceNet + SQLite DB
# ==============================================================================

import os
import sys

# เพิ่ม parent directory เข้า sys.path เพื่อให้สามารถเรียก database.py และ face_engine.py ได้
parent_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if parent_dir not in sys.path:
    sys.path.insert(0, parent_dir)

from database import FaceDatabaseManager, DEFAULT_DB_PATH
from face_engine import MTCNNDetector, MobileFaceNetExtractor, DEFAULT_ONNX_PATH
from realtime_recognition import RealTimeFaceRecognitionApp

def main():
    print("=" * 70)
    print("  เริ่มต้นระบบ Face Recognition (MTCNN + MobileFaceNet + SQLite DB)")
    print("=" * 70)
    app = RealTimeFaceRecognitionApp(
        db_path=DEFAULT_DB_PATH,
        model_path=DEFAULT_ONNX_PATH,
        similarity_threshold=0.60,
        camera_id=0
    )
    try:
        app.run()
    except Exception as e:
        print(f"[!] เกิดข้อผิดพลาดในการรันระบบ: {e}")

if __name__ == "__main__":
    main()
