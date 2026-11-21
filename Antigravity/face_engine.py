# face_engine.py
# ==============================================================================
# โครงงาน: Face Recognition on Raspberry Pi 5 (MobileFaceNet + INT8 Quantization)
# หน้าที่: Core Face Engine รวม MTCNN (Detection & Alignment) + MobileFaceNet (Embeddings)
# ==============================================================================

import os
import sys
import time
import cv2
import numpy as np
from PIL import Image
from typing import List, Tuple, Dict, Optional, Any

os.environ["QT_QPA_PLATFORM"] = "xcb"
os.environ["QT_LOGGING_RULES"] = "*.debug=false;qt.qpa.*=false;QFontDatabase.*=false"

try:
    import torch
    num_cpus = os.cpu_count() or 4
    torch.set_num_threads(num_cpus)
    torch.set_num_interop_threads(min(2, num_cpus))
except Exception:
    pass

if sys.stdout and hasattr(sys.stdout, 'reconfigure'):
    try:
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    except Exception:
        pass

# พาทโมเดล MobileFaceNet
MODEL_DIR = os.path.join(os.path.dirname(__file__), "my_face_project", "models")
DEFAULT_ONNX_PATH = os.path.join(MODEL_DIR, "mobilefacenet.onnx")
if not os.path.exists(DEFAULT_ONNX_PATH):
    DEFAULT_ONNX_PATH = os.path.join(os.path.dirname(__file__), "mobilefacenet.onnx")


# พิกัด Landmark อ้างอิงมาตรฐาน 5 จุด (Canonical 112x112 ArcFace / InsightFace Template)
REFERENCE_5PTS_112 = np.array([
    [38.2946, 51.6963],  # ตาซ้าย
    [73.5318, 51.6963],  # ตาขวา
    [56.0252, 71.7366],  # ปลายจมูก
    [41.5493, 92.3655],  # มุมปากซ้าย
    [70.7299, 92.3655]   # มุมปากขวา
], dtype=np.float32)


def align_face_5pts(img_bgr: np.ndarray, landmarks: List[List[float]], target_size: int = 112) -> np.ndarray:
    """ทำ 5-Point Affine Similarity Alignment ตามมาตรฐานสากล เพื่อความคมชัดและความแม่นยำสูงสุด"""
    if landmarks and len(landmarks) == 5:
        src = np.array(landmarks, dtype=np.float32)
        M, _ = cv2.estimateAffinePartial2D(src, REFERENCE_5PTS_112, method=cv2.LMEDS)
        if M is not None:
            return cv2.warpAffine(img_bgr, M, (target_size, target_size), flags=cv2.INTER_CUBIC, borderMode=cv2.BORDER_REFLECT)
    return cv2.resize(img_bgr, (target_size, target_size), interpolation=cv2.INTER_CUBIC)


class MTCNNDetector:
    """
    Face Detector (Multi-Backend Support):
      1. YuNet (OpenCV DNN ONNX): High-speed deep face detector (15-20ms on Pi 5 CPU) with Auto-Scaling
      2. MTCNN (facenet_pytorch P-Net, R-Net, O-Net): Cascaded PyTorch CNN
      3. OpenCV Haar Cascade (Fast fallback)
    """

    def __init__(self, image_size: int = 112, margin: int = 14, min_face_size: int = 35, factor: float = 0.709, device: str = 'cpu'):
        self.image_size = image_size
        self.margin = margin
        self.device = device
        self.backend = "unknown"

        # 1. ทดลองโหลดโมเดล YuNet (ความเร็วสูงสุด 15-25ms บน Raspberry Pi 5)
        yunet_path = os.path.join(os.path.dirname(__file__), "face_detection_yunet_2023mar.onnx")
        if not os.path.exists(yunet_path):
            yunet_path = os.path.join(MODEL_DIR, "face_detection_yunet_2023mar.onnx")

        if os.path.exists(yunet_path) and hasattr(cv2, 'FaceDetectorYN'):
            try:
                self.yunet_path = yunet_path
                self.yunet = cv2.FaceDetectorYN.create(
                    yunet_path, "", (640, 480), score_threshold=0.55, nms_threshold=0.3, top_k=10
                )
                self.backend = "yunet"
                print(f"[OK] โหลดโมเดล Face Detector (YuNet ONNX) สำเร็จบน OpenCV DNN (Edge Optimized)")
            except Exception as e:
                print(f"[!] ไม่สามารถเริ่ม YuNet ได้ ({e}) กำลังใช้ MTCNN fallback")

        # 2. Fallback MTCNN
        if self.backend == "unknown":
            try:
                from facenet_pytorch import MTCNN
                self.mtcnn = MTCNN(
                    image_size=image_size,
                    margin=margin,
                    min_face_size=min_face_size,
                    thresholds=[0.6, 0.7, 0.7],
                    factor=factor,
                    post_process=False,
                    keep_all=True,
                    device=device
                )
                self.backend = "facenet_pytorch"
                print(f"[OK] โหลดโมเดล MTCNN (P-Net, R-Net, O-Net) สำเร็จบนอุปกรณ์: {device}")
            except Exception as e:
                print(f"[!] คำเตือน: facenet_pytorch MTCNN ใช้งานไม่ได้ ({e}) จะใช้ OpenCV Cascade Fallback")
                haar_path = os.path.join(MODEL_DIR, "haarcascade_frontalface_default.xml")
                if not os.path.exists(haar_path) and hasattr(cv2, 'data') and hasattr(cv2.data, 'haarcascades'):
                    haar_path = cv2.data.haarcascades + "haarcascade_frontalface_default.xml"
                self.face_cascade = cv2.CascadeClassifier(haar_path)
                self.backend = "opencv_haar"

    def detect_and_align(self, img_bgr: np.ndarray) -> List[Dict[str, Any]]:
        """
        ตรวจจับใบหน้าและทำ 5-Point Alignment
        รองรับภาพความละเอียดสูง (Auto-scale down to max 1280px for accurate receptive field)
        Returns: list of dicts:
          - 'box': [x1, y1, x2, y2]
          - 'confidence': float
          - 'landmarks': 5 points (left_eye, right_eye, nose, mouth_left, mouth_right)
          - 'aligned_face_bgr': 112x112 numpy array
        """
        h, w = img_bgr.shape[:2]
        faces_out = []

        if self.backend == "yunet":
            target_max = 1280
            scale = 1.0
            if max(h, w) > target_max:
                scale = target_max / float(max(h, w))
                det_w = int(w * scale)
                det_h = int(h * scale)
                det_img = cv2.resize(img_bgr, (det_w, det_h), interpolation=cv2.INTER_AREA)
            else:
                det_w = w
                det_h = h
                det_img = img_bgr

            self.yunet.setInputSize((det_w, det_h))
            retval, detected = self.yunet.detect(det_img)
            if detected is not None:
                for f in detected:
                    conf = float(f[-1])
                    if conf < 0.50:
                        continue
                    bx, by, bw, bh = [int(v / scale) for v in f[0:4]]
                    if bw < 15 or bh < 15:
                        continue
                    x1 = max(0, min(w - 1, bx))
                    y1 = max(0, min(h - 1, by))
                    x2 = max(0, min(w, bx + bw))
                    y2 = max(0, min(h, by + bh))
                    if x2 - x1 < 10 or y2 - y1 < 10:
                        continue

                    raw_lms = f[4:14].reshape((5, 2)) / scale
                    landmarks = [[float(pt[0]), float(pt[1])] for pt in raw_lms]

                    # ทำ 5-Point Affine Similarity Alignment จากภาพต้นฉบับคมชัดสูงสุด
                    face_aligned = align_face_5pts(img_bgr, landmarks, self.image_size)

                    faces_out.append({
                        "box": [x1, y1, x2, y2],
                        "confidence": conf,
                        "landmarks": landmarks,
                        "aligned_face_bgr": face_aligned
                    })

            # จัดอันดับใบหน้าหลัก: พิจารณาขนาดพื้นที่และตำแหน่งใกล้จุดกึ่งกลางภาพ
            img_cx, img_cy = w / 2.0, h / 2.0
            def rank_score(face):
                box = face["box"]
                fcx = (box[0] + box[2]) / 2.0
                fcy = (box[1] + box[3]) / 2.0
                dist = np.hypot(fcx - img_cx, fcy - img_cy)
                area = (box[2] - box[0]) * (box[3] - box[1])
                return (area / (dist + 120.0)) * face["confidence"]

            faces_out.sort(key=rank_score, reverse=True)

        elif self.backend == "facenet_pytorch":
            img_rgb = cv2.cvtColor(img_bgr, cv2.COLOR_BGR2RGB)
            img_pil = Image.fromarray(img_rgb)

            boxes, probs, landmarks = self.mtcnn.detect(img_pil, landmarks=True)

            if boxes is not None:
                for idx, box in enumerate(boxes):
                    prob = float(probs[idx]) if probs is not None else 1.0
                    if prob < 0.5:
                        continue

                    x1, y1, x2, y2 = [int(v) for v in box]
                    # Clamp to image boundaries
                    x1 = max(0, min(w - 1, x1))
                    y1 = max(0, min(h - 1, y1))
                    x2 = max(0, min(w, x2))
                    y2 = max(0, min(h, y2))

                    if x2 - x1 < 10 or y2 - y1 < 10:
                        continue

                    lm_list = []
                    if landmarks is not None and len(landmarks) > idx:
                        lm_list = [[float(pt[0]), float(pt[1])] for pt in landmarks[idx]]

                    # Crop and align
                    if lm_list and len(lm_list) == 5:
                        face_aligned = align_face_5pts(img_bgr, lm_list, self.image_size)
                    else:
                        face_crop = img_bgr[y1:y2, x1:x2]
                        face_aligned = cv2.resize(face_crop, (self.image_size, self.image_size), interpolation=cv2.INTER_AREA)

                    faces_out.append({
                        "box": [x1, y1, x2, y2],
                        "confidence": prob,
                        "landmarks": lm_list,
                        "aligned_face_bgr": face_aligned
                    })
        else:
            # OpenCV Fallback
            gray = cv2.cvtColor(img_bgr, cv2.COLOR_BGR2GRAY)
            detected = self.face_cascade.detectMultiScale(gray, scaleFactor=1.1, minNeighbors=5, minSize=(30, 30))
            for (x, y, bw, bh) in detected:
                x1, y1, x2, y2 = x, y, x + bw, y + bh
                face_crop = img_bgr[y1:y2, x1:x2]
                face_aligned = cv2.resize(face_crop, (self.image_size, self.image_size), interpolation=cv2.INTER_AREA)
                faces_out.append({
                    "box": [x1, y1, x2, y2],
                    "confidence": 0.95,
                    "landmarks": [],
                    "aligned_face_bgr": face_aligned
                })

        return faces_out


class MobileFaceNetExtractor:
    """
    MobileFaceNet: โครงข่ายสำหรับสกัดเวกเตอร์เอกลักษณ์ใบหน้า (MobileFaceNet Embedding 128-d)
    - รองรับ ONNX Runtime Engine และ OpenCV DNN Engine ความเร็วสูงสำหรับ Raspberry Pi 5
    - Input: 112x112 BGR Aligned Face
    - Output: 128-d L2-normalized Face Embedding Vector
    """

    def __init__(self, model_path: str = DEFAULT_ONNX_PATH, device: str = 'cpu'):
        self.model_path = model_path
        self.device = device
        self.ort_sess = None
        self.cv_net = None
        self.torch_model = None
        self.input_name = None
        self.input_shape = (112, 112)

        # ค้นหาไฟล์โมเดล MobileFaceNet ONNX
        mfn_candidates = [
            os.path.join(os.path.dirname(__file__), "mobilefacenet.onnx"),
            os.path.join(MODEL_DIR, "mobilefacenet.onnx"),
            os.path.join(os.path.dirname(__file__), "facenet_512d.onnx"),
            model_path
        ]
        target_onnx = None
        for p in mfn_candidates:
            if p and os.path.exists(p):
                target_onnx = p
                break

        # 1. ลองใช้ ONNX Runtime บน CPU
        if target_onnx:
            try:
                import onnxruntime as ort
                opts = ort.SessionOptions()
                opts.intra_op_num_threads = os.cpu_count() or 4
                opts.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
                self.ort_sess = ort.InferenceSession(target_onnx, sess_options=opts, providers=['CPUExecutionProvider'])
                self.input_name = self.ort_sess.get_inputs()[0].name
                shape = self.ort_sess.get_inputs()[0].shape
                if len(shape) == 4 and shape[2] and shape[3]:
                    self.input_shape = (int(shape[3]), int(shape[2]))
                print(f"[OK] โหลดโมเดล Face Feature Extractor (MobileFaceNet ONNX Runtime) สำเร็จ: {os.path.basename(target_onnx)} (Input: {self.input_shape})")
            except Exception as e:
                # 2. ลองใช้ OpenCV DNN readNetFromONNX (ไม่ต้องใช้แพ็กเกจ onnxruntime เพิ่ม)
                try:
                    self.cv_net = cv2.dnn.readNetFromONNX(target_onnx)
                    self.input_shape = (112, 112)
                    print(f"[OK] โหลดโมเดล Face Feature Extractor (MobileFaceNet OpenCV DNN) สำเร็จ: {os.path.basename(target_onnx)}")
                except Exception as e2:
                    print(f"[!] ONNX Runtime & OpenCV DNN error ({e}, {e2}) กำลังใช้ PyTorch Fallback")

        # 3. PyTorch Fallback
        if self.ort_sess is None and self.cv_net is None:
            try:
                from facenet_pytorch import InceptionResnetV1
                self.torch_model = InceptionResnetV1(pretrained='vggface2').eval().to(device)
                self.input_shape = (160, 160)
                print(f"[OK] โหลดโมเดล Face Feature Extractor (PyTorch Fallback) สำเร็จบนอุปกรณ์: {device}")
            except Exception as e:
                # ลอง OpenCV DNN อีกครั้ง
                if target_onnx and os.path.exists(target_onnx):
                    self.cv_net = cv2.dnn.readNetFromONNX(target_onnx)
                    self.input_shape = (112, 112)
                    print(f"[OK] โหลดโมเดล Face Feature Extractor (MobileFaceNet OpenCV DNN) สำเร็จ")
                else:
                    raise RuntimeError(f"ไม่สามารถโหลดโมเดล Feature Extractor ได้: {e}")

    def extract_embedding(self, aligned_face_bgr: np.ndarray) -> np.ndarray:
        """
        รับภาพใบหน้า BGR ทำการแปลงเป็น RGB, Normalize [-1, 1], และคำนวณ Embedding พร้อม L2-Normalization
        """
        if self.ort_sess is not None:
            face_rgb = cv2.cvtColor(aligned_face_bgr, cv2.COLOR_BGR2RGB)
            face_resized = cv2.resize(face_rgb, self.input_shape, interpolation=cv2.INTER_AREA).astype(np.float32)
            face_norm = (face_resized - 127.5) / 128.0
            tensor = np.transpose(face_norm, (2, 0, 1))[np.newaxis, ...]
            raw_emb = self.ort_sess.run(None, {self.input_name: tensor})[0].flatten()
            norm = np.linalg.norm(raw_emb)
            return (raw_emb / norm).astype(np.float32) if norm > 0 else raw_emb.astype(np.float32)

        elif self.cv_net is not None:
            face_rgb = cv2.cvtColor(aligned_face_bgr, cv2.COLOR_BGR2RGB)
            blob = cv2.dnn.blobFromImage(
                face_rgb,
                scalefactor=1.0 / 128.0,
                size=self.input_shape,
                mean=(127.5, 127.5, 127.5),
                swapRB=False
            )
            self.cv_net.setInput(blob)
            raw_emb = self.cv_net.forward().flatten()
            norm = np.linalg.norm(raw_emb)
            return (raw_emb / norm).astype(np.float32) if norm > 0 else raw_emb.astype(np.float32)

        elif self.torch_model is not None:
            import torch
            face_rgb = cv2.cvtColor(aligned_face_bgr, cv2.COLOR_BGR2RGB)
            face_resized = cv2.resize(face_rgb, (160, 160), interpolation=cv2.INTER_AREA).astype(np.float32)
            face_norm = (face_resized - 127.5) / 128.0
            tensor = torch.from_numpy(face_norm).permute(2, 0, 1).unsqueeze(0).float().to(self.device)
            with torch.no_grad():
                raw_emb = self.torch_model(tensor).cpu().numpy().flatten()
            norm = np.linalg.norm(raw_emb)
            return (raw_emb / norm).astype(np.float32) if norm > 0 else raw_emb.astype(np.float32)

        return np.zeros(128, dtype=np.float32)


class FaceRecognitionPipeline:
    """
    ระบบ Pipeline สมบูรณ์ตามสไลด์ PowerPoint:
    Input Camera/Image -> MTCNN Detection & Alignment -> MobileFaceNet Embedding -> SQLite Database Cosine Search -> Known/Unknown
    """

    def __init__(self, db_path: Optional[str] = None, model_path: str = DEFAULT_ONNX_PATH):
        try:
            from database import FaceDatabaseManager, DEFAULT_DB_PATH
        except ImportError:
            from Antigravity.database import FaceDatabaseManager, DEFAULT_DB_PATH
        self.db = FaceDatabaseManager(db_path or DEFAULT_DB_PATH)
        self.detector = MTCNNDetector(image_size=112)
        self.extractor = MobileFaceNetExtractor(model_path)

    def process_frame(self, frame_bgr: np.ndarray, threshold: float = 0.60, log_to_db: bool = False) -> List[Dict[str, Any]]:
        """
        ประมวลผลเฟรมภาพ 1 เฟรม
        Returns ข้อมูลใบหน้าทั้งหมดที่ตรวจพบ พร้อมผลการเปรียบเทียบจาก SQLite DB
        """
        t0 = time.perf_counter()
        detected_faces = self.detector.detect_and_align(frame_bgr)
        t_detect = (time.perf_counter() - t0) * 1000.0

        results = []
        for face_info in detected_faces:
            aligned_face = face_info["aligned_face_bgr"]

            # Feature Extraction
            t1 = time.perf_counter()
            embedding = self.extractor.extract_embedding(aligned_face)
            t_embed = (time.perf_counter() - t1) * 1000.0

            # SQLite Database Search
            t2 = time.perf_counter()
            matched_name, similarity, matched_person_id, status = self.db.find_match(embedding, threshold=threshold)
            t_match = (time.perf_counter() - t2) * 1000.0

            total_lat = t_detect + t_embed + t_match

            # บันทึกลง Log ถ้าเปิดใช้งาน
            if log_to_db:
                self.db.log_recognition(
                    matched_name=matched_name,
                    similarity_score=similarity,
                    threshold=threshold,
                    status=status,
                    matched_person_id=matched_person_id,
                    detection_time_ms=round(t_detect, 2),
                    embedding_time_ms=round(t_embed, 2),
                    matching_time_ms=round(t_match, 2),
                    total_latency_ms=round(total_lat, 2)
                )

            results.append({
                "box": face_info["box"],
                "confidence": face_info["confidence"],
                "landmarks": face_info["landmarks"],
                "embedding": embedding,
                "name": matched_name,
                "similarity": similarity,
                "status": status,
                "person_id": matched_person_id,
                "aligned_face": aligned_face,
                "timing": {
                    "detect_ms": round(t_detect, 2),
                    "embed_ms": round(t_embed, 2),
                    "match_ms": round(t_match, 2),
                    "total_ms": round(total_lat, 2)
                }
            })

        return results


if __name__ == "__main__":
    print("=" * 65)
    print("  ทดสอบ Face Recognition Pipeline (MTCNN + MobileFaceNet)")
    print("=" * 65)

    pipeline = FaceRecognitionPipeline()
    print("[OK] Pipeline เริ่มต้นสำเร็จ!")

    # ทดสอบกับภาพในโฟลเดอร์ known_faces
    known_dir = os.path.join(os.path.dirname(__file__), "my_face_project", "known_faces")
    if os.path.exists(known_dir):
        for img_name in os.listdir(known_dir):
            if img_name.lower().endswith(('.jpg', '.png', '.jpeg')):
                img_path = os.path.join(known_dir, img_name)
                img = cv2.imread(img_path)
                if img is not None:
                    res = pipeline.process_frame(img, threshold=0.60)
                    print(f"\n[*] ประมวลผลภาพ: {img_name}")
                    for r in res:
                        print(f"    - ผลการรู้จำ: {r['name']} (Similarity: {r['similarity']:.4f}, สถานะ: {r['status']})")
                        print(f"    - MTCNN Conf: {r['confidence']:.3f} | BBox: {r['box']}")
                        print(f"    - ความเร็ว: Detect={r['timing']['detect_ms']}ms, Embed={r['timing']['embed_ms']}ms, Match={r['timing']['match_ms']}ms (Total: {r['timing']['total_ms']}ms)")
    print("=" * 65)
