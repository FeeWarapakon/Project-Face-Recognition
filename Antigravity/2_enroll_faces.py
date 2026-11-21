# 2_enroll_faces.py
# ==============================================================================
# โครงงาน: Face Recognition on Raspberry Pi 5 (MobileFaceNet + INT8 Quantization)
# หน้าที่: บันทึกใบหน้าบุคคล (Face Enrollment) ผ่าน MTCNN + MobileFaceNet ลงฐานข้อมูล SQLite
# รองรับ: 1 คนสามารถมีได้หลายรูปภาพ (Multi-image enrollment ต่อ 1 Person ID)
# ==============================================================================

import os
import sys
import re
import cv2
import time
import argparse
import numpy as np
from PIL import Image

if sys.stdout and hasattr(sys.stdout, 'reconfigure'):
    try:
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    except Exception:
        pass
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from database import FaceDatabaseManager, DEFAULT_DB_PATH
from face_engine import MTCNNDetector, MobileFaceNetExtractor, DEFAULT_ONNX_PATH


class FaceEnrollmentApp:
    def __init__(self, db_path: str = DEFAULT_DB_PATH, model_path: str = DEFAULT_ONNX_PATH):
        self.db = FaceDatabaseManager(db_path)
        self.detector = MTCNNDetector(image_size=112)
        self.extractor = MobileFaceNetExtractor(model_path)

        # รายชื่อสมาชิกกลุ่มโครงงานและคีย์คำค้นหาที่เกี่ยวข้อง (Aliases)
        self.members_info = [
            {
                "full_name": "Narawich Anuwong",
                "student_id": "68543206118-0",
                "role": "Student / Project Member",
                "aliases": ["narawich", "anuwong", "นราวิชญ์", "อนุวงศ์"]
            },
            {
                "full_name": "Sanya Lungjay",
                "student_id": "68543206057-0",
                "role": "Student / Project Member",
                "aliases": ["sanya", "sunya", "lungjay", "pomking", "สัญญา", "ลุงจาย"]
            },
            {
                "full_name": "Warapakon Phimwong",
                "student_id": "68543206123-0",
                "role": "Student / Project Member",
                "aliases": ["warapakorn", "warapakon", "pimwong", "phimwong", "วราปกรณ์", "พิมพ์วงษ์"]
            }
        ]

    def _resolve_person_info(self, raw_name: str) -> tuple:
        """แปลงชื่อไฟล์หรือชื่อโฟลเดอร์ให้เป็นข้อมูลบุคคล (Full Name, Student ID, Role)"""
        # ตัด suffix ตัวเลข เช่น Narawich_1, Narawich-2, Narawich (3), Narawich_front
        cleaned_key = re.sub(r'[\s_\-\(]+(\d+|front|side|left|right|pic\d*)\)?$', '', raw_name, flags=re.IGNORECASE).strip().lower()

        # ค้นหาว่าตรงกับสมาชิกกลุ่มหรือไม่ (จากชื่อ, นามสกุล, หรือชื่อเล่น)
        for member in self.members_info:
            for alias in member["aliases"]:
                if alias in cleaned_key:
                    return (member["full_name"], member["student_id"], member["role"])

        clean_display_name = re.sub(r'[\s_\-\(]+(\d+|front|side|left|right|pic\d*)\)?$', '', raw_name, flags=re.IGNORECASE).strip()
        return (clean_display_name if clean_display_name else raw_name, "N/A", "General Member")

    def enroll_single_image(
        self,
        name: str,
        image_path: str,
        student_id: str = "",
        role: str = "Student / Developer",
        overwrite: bool = False
    ) -> bool:
        """ลงทะเบียนใบหน้าจากไฟล์รูปภาพ 1 รูป"""
        if not os.path.exists(image_path):
            print(f"[!] ไม่พบไฟล์รูปภาพ: {image_path}")
            return False

        img = cv2.imread(image_path)
        if img is None:
            print(f"[!] ไม่สามารถอ่านไฟล์รูปภาพได้: {image_path}")
            return False

        faces = self.detector.detect_and_align(img)
        if not faces:
            print(f"[!] MTCNN ไม่พบใบหน้าในรูปภาพ: {image_path}")
            return False

        # เลือกใบหน้าหลักของบุคคล (ผ่านการจัดอันดับจาก Detector แล้ว)
        best_face = faces[0]
        aligned_face = best_face["aligned_face_bgr"]
        conf = best_face["confidence"]
        landmarks = best_face["landmarks"]

        # สกัด Feature Embedding Vector
        embedding = self.extractor.extract_embedding(aligned_face)

        # บันทึกลง SQLite
        person_id = self.db.register_person_with_embedding(
            name=name,
            embedding=embedding,
            student_id=student_id,
            role=role,
            source_image=image_path,
            mtcnn_confidence=conf,
            landmarks=landmarks,
            overwrite_embeddings=overwrite
        )

        img_filename = os.path.basename(image_path)
        print(f"  [+] บันทึกภาพ '{img_filename}' -> '{name}' | ID: {person_id} | MTCNN Conf: {conf:.4f} | Vector Dim: {embedding.size}")
        return True

    def enroll_from_folder(self, folder_path: str, overwrite: bool = False):
        """
        ลงทะเบียนรูปภาพทั้งหมดในโฟลเดอร์ โดยรองรับ:
        1. โฟลเดอร์ย่อย: folder_path/Narawich/1.jpg, folder_path/Narawich/2.jpg
        2. ไฟล์รูปภาพรวม: folder_path/Narawich_1.jpg, folder_path/Narawich_2.jpg
        """
        if not os.path.exists(folder_path):
            print(f"[!] ไม่พบโฟลเดอร์: {folder_path}")
            return

        valid_exts = ('.jpg', '.jpeg', '.png', '.bmp')
        
        # รวบรวมไฟล์ภาพทั้งหมด
        images_to_process = []

        for root, dirs, files in os.walk(folder_path):
            for f in files:
                if f.lower().endswith(valid_exts):
                    full_path = os.path.join(root, f)
                    rel_path = os.path.relpath(full_path, folder_path)
                    parts = rel_path.split(os.sep)

                    if len(parts) > 1:
                        # กรณีอยู่ใน Subfolder เช่น known_faces/Narawich Anuwong/1.jpg -> ใช้ชื่อโฟลเดอร์ย่อย
                        raw_person_key = parts[0]
                    else:
                        # กรณีอยู่ใน Root folder เช่น known_faces/Narawich_1.jpg -> ใช้ชื่อไฟล์ตัดนามสกุล
                        raw_person_key = os.path.splitext(f)[0]

                    images_to_process.append((raw_person_key, full_path))

        if not images_to_process:
            print(f"[!] ไม่พบไฟล์ภาพในโฟลเดอร์: {folder_path}")
            return

        mode_text = "ล้างเวกเตอร์เก่าแล้วบันทึกใหม่ (Overwrite)" if overwrite else "เพิ่มเวกเตอร์ต่อยอด (Append/Multi-image)"
        print(f"\n[*] พบภาพทั้งหมด {len(images_to_process)} ไฟล์ ในโฟลเดอร์ '{folder_path}'")
        print(f"[*] โหมดการลงทะเบียน: {mode_text}")
        print("=" * 80)

        processed_persons = set()
        success_count = 0

        for raw_key, img_path in images_to_process:
            full_name, std_id, role = self._resolve_person_info(raw_key)

            # หากระบุ overwrite=True จะล้างเวกเตอร์เดิมเฉพาะครั้งแรกที่เจอบุคคลนั้นในการรันนี้
            should_overwrite_this_time = overwrite and (full_name not in processed_persons)

            if self.enroll_single_image(full_name, img_path, student_id=std_id, role=role, overwrite=should_overwrite_this_time):
                success_count += 1
                processed_persons.add(full_name)

        # ล้างรายชื่อบุคคลเก่าในฐานข้อมูลที่ไม่มีเวกเตอร์รูปภาพหลงเหลืออยู่ (0 รูปภาพ)
        deleted = self.db.cleanup_empty_persons()
        if deleted > 0:
            print(f"[*] ล้างรายการบุคคลว่าง (0 เวกเตอร์) ออกจากฐานข้อมูล: {deleted} รายการ")

        print("-" * 80)
        print(f"[OK] ลงทะเบียนเสร็จสิ้น: สำเร็จ {success_count}/{len(images_to_process)} รูปภาพ (รวม {len(processed_persons)} บุคคล)")

    def enroll_from_camera(self, name: str, student_id: str = "", role: str = "Student", camera_id: int = 0):
        """เปิดกล้องเพื่อจับภาพและลงทะเบียนแบบสดๆ ด้วย MTCNN (สามารถกด SPACEBAR เพื่อบันทึกหลายรูปได้)"""
        backend = cv2.CAP_DSHOW if os.name == 'nt' else cv2.CAP_ANY
        cap = cv2.VideoCapture(camera_id, backend)
        if not cap.isOpened():
            # ลองค้นหากล้องที่เชื่อมต่ออัตโนมัติ
            for cand_id in [3, 1, 2, 4, 0]:
                test_cap = cv2.VideoCapture(cand_id, backend)
                if test_cap.isOpened():
                    ret, _ = test_cap.read()
                    if ret:
                        cap = test_cap
                        camera_id = cand_id
                        print(f"[*] ตรวจพบและเปิดใช้งานกล้อง Device ID: {cand_id}")
                        break
                    test_cap.release()

        if not cap.isOpened():
            print(f"[!] ไม่สามารถเปิดกล้อง ID {camera_id} ได้ กรุณาตรวจสอบการเชื่อมต่อ")
            return

        cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*'MJPG'))
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)

        print("=" * 65)
        print(f"  [CAMERA ENROLLMENT] กำลังลงทะเบียน: '{name}'")
        print("  - หันหน้าตรง/เอียงใบหน้าเข้าหากล้อง")
        print("  - กด 'SPACEBAR' เพื่อบันทึกภาพ (สามารถกดบันทึกได้หลายมุมมอง)")
        print("  - กด 'ESC' หรือ 'Q' เพื่อเสร็จสิ้นและออกจากกล้อง")
        print("=" * 65)

        captured_count = 0
        while cap.isOpened():
            ret, frame = cap.read()
            if not ret:
                break

            display = frame.copy()
            faces = self.detector.detect_and_align(frame)

            face_detected = len(faces) > 0
            if face_detected:
                best_face = max(faces, key=lambda f: f["confidence"])
                # แสดงเฉพาะข้อความแนะนำสถานะ ไม่วาดกรอบใบหน้าบนภาพ
                cv2.putText(display, f"Face Ready | Captured: {captured_count} (Press SPACE to capture)",
                            (30, 40), cv2.FONT_HERSHEY_SIMPLEX, 0.65, (0, 255, 0), 2)
            else:
                cv2.putText(display, f"Looking for face... | Captured: {captured_count}",
                            (30, 40), cv2.FONT_HERSHEY_SIMPLEX, 0.65, (0, 165, 255), 2)

            cv2.imshow("Face Enrollment (Clean Camera)", display)
            key = cv2.waitKey(1) & 0xFF

            if key == 32 and face_detected:  # Spacebar
                os.makedirs("enrolled_snapshots", exist_ok=True)
                snapshot_file = os.path.join("enrolled_snapshots", f"{name.replace(' ', '_')}_{int(time.time())}_{captured_count+1}.jpg")
                cv2.imwrite(snapshot_file, frame)

                aligned = best_face["aligned_face_bgr"]
                embedding = self.extractor.extract_embedding(aligned)

                self.db.register_person_with_embedding(
                    name=name,
                    embedding=embedding,
                    student_id=student_id,
                    role=role,
                    source_image=snapshot_file,
                    mtcnn_confidence=best_face["confidence"],
                    landmarks=best_face["landmarks"],
                    overwrite_embeddings=False
                )
                captured_count += 1
                print(f"[OK] บันทึกภาพที่ {captured_count} ของ '{name}' ลงในฐานข้อมูล SQLite สำเร็จ!")
            elif key in (27, ord('q')):
                print(f"[!] สิ้นสุดการลงทะเบียนผ่านกล้อง (บันทึกไปทั้งหมด {captured_count} รูปภาพ)")
                break

        cap.release()
        cv2.destroyAllWindows()

    def list_enrolled_faces(self, clean_empty: bool = True):
        """แสดงสรุปรายชื่อบุคคลและจำนวนเวกเตอร์รูปภาพทั้งหมดใน SQLite"""
        if clean_empty:
            self.db.cleanup_empty_persons()

        summary = self.db.get_persons_summary()
        all_embeddings = self.db.get_all_embeddings()

        print("\n" + "=" * 80)
        print(f"  รายชื่อบุคคลและเวกเตอร์ที่ลงทะเบียนใน SQLite (รวม {len(summary)} บุคคล, {len(all_embeddings)} เวกเตอร์)")
        print("=" * 80)
        print(f"{'ID':<4} | {'ชื่อ-นามสกุล':<34} | {'รหัสนักศึกษา':<16} | {'จำนวนรูปภาพ/เวกเตอร์':<15}")
        print("-" * 80)
        for r in summary:
            count = r['embedding_count'] or 0
            count_str = f"{count} รูปภาพ ({count} เวกเตอร์)"
            print(f"{r['person_id']:<4} | {r['name']:<34} | {str(r['student_id'] or 'N/A'):<16} | {count_str:<15}")
        print("=" * 80 + "\n")


def main():
    parser = argparse.ArgumentParser(description="MTCNN + MobileFaceNet Multi-Image Face Enrollment into SQLite DB")
    parser.add_argument("--folder", "-f", type=str, default="my_face_project/known_faces", help="Folder containing known face images")
    parser.add_argument("--camera", "-c", action="store_true", help="Enroll via Webcam")
    parser.add_argument("--name", "-n", type=str, default="", help="Person name for camera enrollment")
    parser.add_argument("--student_id", "-s", type=str, default="", help="Student ID")
    parser.add_argument("--overwrite", action="store_true", help="Overwrite existing embeddings instead of appending")
    parser.add_argument("--reset", action="store_true", help="Reset database and re-enroll completely")
    parser.add_argument("--list", "-l", action="store_true", help="List enrolled members and embedding counts")

    args = parser.parse_args()
    app = FaceEnrollmentApp()

    if args.reset:
        app.db.reset_database()
        print("[OK] ล้างข้อมูลในฐานข้อมูล SQLite เรียบร้อยแล้ว กำลังลงทะเบียนใหม่จากโฟลเดอร์...")
        app.enroll_from_folder(args.folder, overwrite=False)
        app.list_enrolled_faces()
    elif args.list:
        app.list_enrolled_faces()
    elif args.camera:
        name = args.name or input("ระบุชื่อบุคคลที่ต้องการลงทะเบียน: ").strip()
        std_id = args.student_id or input("ระบุรหัสนักศึกษา (ถ้ามี): ").strip()
        if name:
            app.enroll_from_camera(name, student_id=std_id)
            app.list_enrolled_faces()
    else:
        # Default: Enroll from folder
        app.enroll_from_folder(args.folder, overwrite=args.overwrite)
        app.list_enrolled_faces()


if __name__ == "__main__":
    main()
