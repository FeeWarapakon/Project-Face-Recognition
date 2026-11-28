# 5_accuracy_eval.py
# ==============================================================================
# โครงงาน: Face Recognition on Raspberry Pi 5 (MobileFaceNet + INT8 Quantization)
# หน้าที่: ประเมินความแม่นยำของระบบอย่างเป็นทางการตามหลักสถิติ (Confusion Matrix)
#          คำนวณ Accuracy, Precision, Recall, F1-Score, FAR, FRR
#          พร้อมเปรียบเทียบ FP32 vs INT8 และ Threshold Sweep สำหรับเล่มรายงานและสไลด์สอบ
# ==============================================================================

import os
import sys
import glob
import time
import argparse
import itertools
import cv2
import numpy as np
from typing import Dict, List, Tuple, Any

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

if sys.stdout and hasattr(sys.stdout, 'reconfigure'):
    try:
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    except Exception:
        pass

from face_engine import MTCNNDetector, MobileFaceNetExtractor


def cosine_similarity(a: np.ndarray, b: np.ndarray) -> float:
    return float(np.dot(a, b))


def augment_field_conditions(aligned_bgr: np.ndarray) -> List[Tuple[str, np.ndarray]]:
    """
    จำลองสภาพแวดล้อมจริงในการใช้งานหน้ากล้อง (Field Perturbations):
    แสงสว่างมาก/น้อย, เอียงศีรษะเล็กน้อย, กลับด้านกระจก, เบลอเล็กน้อยจากการขยับ
    """
    h, w = aligned_bgr.shape[:2]
    variants = [("Original", aligned_bgr)]

    # 1. Horizontal Flip (Mirror)
    variants.append(("MirrorFlip", cv2.flip(aligned_bgr, 1)))

    # 2. Brighter lighting (+22%)
    bright = cv2.convertScaleAbs(aligned_bgr, alpha=1.18, beta=18)
    variants.append(("BrightLight", bright))

    # 3. Dim lighting (-22%)
    dim = cv2.convertScaleAbs(aligned_bgr, alpha=0.80, beta=-18)
    variants.append(("DimLight", dim))

    # 4. Slight head tilt (+8 deg & -8 deg)
    for deg in (8.0, -8.0):
        M = cv2.getRotationMatrix2D((w * 0.5, h * 0.5), deg, 1.0)
        rot = cv2.warpAffine(aligned_bgr, M, (w, h), borderMode=cv2.BORDER_REFLECT)
        variants.append((f"Tilt_{deg:+.0f}deg", rot))

    # 5. Mild motion blur (simulates walking / slight camera shake)
    blur = cv2.GaussianBlur(aligned_bgr, (3, 3), 0.8)
    variants.append(("MildBlur", blur))

    return variants


def load_person_folders(root_dir: str, detector: MTCNNDetector) -> Dict[str, List[Tuple[str, np.ndarray]]]:
    """โหลดภาพใบหน้าจากโฟลเดอร์ย่อย (ชื่อโฟลเดอร์ = ชื่อบุคคล) แล้วตรวจจับ + Align 112x112"""
    dataset: Dict[str, List[Tuple[str, np.ndarray]]] = {}
    if not os.path.exists(root_dir):
        return dataset

    for person_name in sorted(os.listdir(root_dir)):
        person_dir = os.path.join(root_dir, person_name)
        if not os.path.isdir(person_dir):
            continue
        img_files = sorted(
            glob.glob(os.path.join(person_dir, "*.jpg"))
            + glob.glob(os.path.join(person_dir, "*.jpeg"))
            + glob.glob(os.path.join(person_dir, "*.png"))
        )
        faces_for_person = []
        for fpath in img_files:
            bgr = cv2.imread(fpath)
            if bgr is None:
                continue
            detected = detector.detect_and_align(bgr)
            if detected:
                # เลือกใบหน้าที่มีความมั่นใจสูงสุด
                best = max(detected, key=lambda d: d["confidence"])
                faces_for_person.append((os.path.basename(fpath), best["aligned_face_bgr"]))
        if faces_for_person:
            dataset[person_name] = faces_for_person
    return dataset


def compute_metrics(tp: int, tn: int, fp: int, fn: int) -> Dict[str, float]:
    total = max(1, tp + tn + fp + fn)
    accuracy = (tp + tn) / total * 100.0
    precision = (tp / max(1, tp + fp)) * 100.0
    recall = (tp / max(1, tp + fn)) * 100.0
    f1 = (2.0 * precision * recall / max(1e-6, precision + recall))
    far = (fp / max(1, fp + tn)) * 100.0   # False Acceptance Rate (รับคนผิด)
    frr = (fn / max(1, fn + tp)) * 100.0   # False Rejection Rate (ปฏิเสธคนถูก)
    return {
        "tp": tp, "tn": tn, "fp": fp, "fn": fn, "total": total,
        "accuracy": accuracy,
        "precision": precision,
        "recall": recall,
        "f1": f1,
        "far": far,
        "frr": frr,
    }


def evaluate_pairs(
    genuine_scores: List[float],
    impostor_scores: List[float],
    threshold: float
) -> Dict[str, float]:
    tp = sum(1 for s in genuine_scores if s >= threshold)
    fn = sum(1 for s in genuine_scores if s < threshold)
    fp = sum(1 for s in impostor_scores if s >= threshold)
    tn = sum(1 for s in impostor_scores if s < threshold)
    return compute_metrics(tp, tn, fp, fn)


def run_evaluation_for_model(
    model_label: str,
    model_path: str,
    dataset: Dict[str, List[Tuple[str, np.ndarray]]],
    threshold: float = 0.60,
    use_augmentation: bool = True,
) -> Dict[str, Any]:
    """
    ประเมินโมเดลแบบ Leave-One-Out Verification Pairs:
    - Genuine Pairs (คนเดียวกัน, คนละภาพ / คนละสภาพแสง-มุมหน้า): ควรได้ score >= threshold
    - Impostor Pairs (คนละคนกัน): ควรได้ score < threshold
    """
    extractor = MobileFaceNetExtractor(model_path=model_path)

    # สกัดเวกเตอร์ทั้งหมด + จับเวลาเฉลี่ย
    embeddings_by_person: Dict[str, List[Tuple[str, str, np.ndarray]]] = {}
    latencies_ms: List[float] = []

    for person, face_list in dataset.items():
        embeddings_by_person[person] = []
        for fname, aligned_bgr in face_list:
            variants = augment_field_conditions(aligned_bgr) if use_augmentation else [("Original", aligned_bgr)]
            for vname, vimg in variants:
                t0 = time.perf_counter()
                emb = extractor.extract_embedding(vimg)
                t1 = time.perf_counter()
                latencies_ms.append((t1 - t0) * 1000.0)
                embeddings_by_person[person].append((fname, vname, emb))

    # 1. สร้าง Genuine Pairs (คนเดียวกัน แต่ต้องมาจาก "คนละไฟล์ต้นฉบับ" เพื่อไม่ให้โกงทดสอบภาพเดิม!)
    genuine_scores: List[float] = []
    for person, entries in embeddings_by_person.items():
        for (f1, v1, e1), (f2, v2, e2) in itertools.combinations(entries, 2):
            if f1 != f2:  # Strict Cross-Image Verification (Leave-One-Out)
                genuine_scores.append(cosine_similarity(e1, e2))

    # 2. สร้าง Impostor Pairs (คนละคนกัน)
    impostor_scores: List[float] = []
    person_names = list(embeddings_by_person.keys())
    for p1, p2 in itertools.combinations(person_names, 2):
        for (_, _, e1) in embeddings_by_person[p1]:
            for (_, _, e2) in embeddings_by_person[p2]:
                impostor_scores.append(cosine_similarity(e1, e2))

    # 3. คำนวณ Confusion Matrix ที่ Threshold หลัก
    main_metrics = evaluate_pairs(genuine_scores, impostor_scores, threshold)

    # 4. คำนวณ Threshold Sweep เพื่อหาจุดสมดุลที่เหมาะสมที่สุด
    sweep_thresholds = [0.35, 0.40, 0.45, 0.50, 0.55, 0.60, 0.65, 0.70]
    sweep = {thr: evaluate_pairs(genuine_scores, impostor_scores, thr) for thr in sweep_thresholds}

    return {
        "label": model_label,
        "model_path": model_path,
        "size_mb": os.path.getsize(model_path) / (1024 * 1024) if os.path.exists(model_path) else 0.0,
        "avg_latency_ms": float(np.mean(latencies_ms)) if latencies_ms else 0.0,
        "genuine_count": len(genuine_scores),
        "impostor_count": len(impostor_scores),
        "genuine_mean": float(np.mean(genuine_scores)) if genuine_scores else 0.0,
        "impostor_mean": float(np.mean(impostor_scores)) if impostor_scores else 0.0,
        "metrics": main_metrics,
        "sweep": sweep,
    }


def print_report(results: List[Dict[str, Any]], threshold: float, dataset: Dict[str, Any], export_md: bool = True):
    total_persons = len(dataset)
    total_raw_imgs = sum(len(v) for v in dataset.values())

    print("\n" + "=" * 82)
    print("  รายงานการประเมินความแม่นยำระบบรู้จำใบหน้า (CONFUSION MATRIX EVALUATION REPORT)")
    print("=" * 82)
    print(f"  จำนวนบุคคลในชุดข้อมูล     : {total_persons} บุคคล ({', '.join(dataset.keys())})")
    print(f"  จำนวนภาพต้นฉบับทั้งหมด    : {total_raw_imgs} ภาพ (Cross-Image Leave-One-Out + Field Augment)")
    print(f"  ค่า Threshold ที่ใช้ตัดสิน  : {threshold:.2f} (Cosine Similarity)")
    print("=" * 82)

    for res in results:
        m = res["metrics"]
        print(f"\n>>> โมเดล: {res['label']} ({os.path.basename(res['model_path'])}, {res['size_mb']:.2f} MB)")
        print(f"    - คู่ทดสอบทั้งหมด (Total Pairs) : {m['total']} คู่ (Genuine: {res['genuine_count']} | Impostor: {res['impostor_count']})")
        print(f"    - ค่าเฉลี่ย Cosine Similarity   : คนเดียวกัน = {res['genuine_mean']:.4f} | คนละคน = {res['impostor_mean']:.4f}")
        print(f"    - Latency เฉลี่ยในการสกัดเวกเตอร์ : {res['avg_latency_ms']:.2f} ms")
        print("\n    [Confusion Matrix]")
        print(f"    +-------------------------+--------------------+--------------------+")
        print(f"    |  Actual \\ Predicted     |  Predicted: MATCH  | Predicted: UNKNOWN |")
        print(f"    +-------------------------+--------------------+--------------------+")
        print(f"    |  Actual: SAME PERSON    |  TP = {m['tp']:<12d} |  FN = {m['fn']:<12d} |")
        print(f"    |  Actual: DIFFERENT      |  FP = {m['fp']:<12d} |  TN = {m['tn']:<12d} |")
        print(f"    +-------------------------+--------------------+--------------------+")
        print(f"\n    [ตัวชี้วัดความแม่นยำตามหลักสถิติ (Threshold = {threshold:.2f})]")
        print(f"    • Accuracy  (ความแม่นยำรวม)        : {m['accuracy']:.2f}%  {'[PASS > 90%]' if m['accuracy'] >= 90.0 else '[BELOW 90%]'}")
        print(f"    • Precision (ความถูกต้องเมื่อทายว่าใช่): {m['precision']:.2f}%")
        print(f"    • Recall    (ความครบถ้วนในการจับถูก) : {m['recall']:.2f}%")
        print(f"    • F1-Score  (ค่าสมดุล Precision/Rec): {m['f1']:.2f}%")
        print(f"    • FAR       (อัตรารับคนผิดเข้าสู่ระบบ) : {m['far']:.2f}%")
        print(f"    • FRR       (อัตราปฏิเสธคนถูก)       : {m['frr']:.2f}%")

        print("\n    [Threshold Sweep Analysis — เปรียบเทียบผลในแต่ละค่า Threshold]")
        print(f"    {'Threshold':>10} | {'Accuracy':>10} | {'Precision':>10} | {'Recall':>10} | {'FAR':>8} | {'FRR':>8}")
        print("    " + "-" * 68)
        for thr, sm in res["sweep"].items():
            mark = " <-- Selected" if abs(thr - threshold) < 1e-6 else ""
            print(f"    {thr:>10.2f} | {sm['accuracy']:>9.2f}% | {sm['precision']:>9.2f}% | {sm['recall']:>9.2f}% | {sm['far']:>7.2f}% | {sm['frr']:>7.2f}%{mark}")

    if len(results) >= 2:
        r_fp32, r_int8 = results[0], results[1]
        print("\n" + "=" * 82)
        print("  ตารางสรุปเปรียบเทียบ FP32 vs INT8 Quantization (พร้อมนำไปใส่สไลด์สอบ)")
        print("=" * 82)
        print(f"  {'ตัวชี้วัด (Metric)':<32} | {'MobileFaceNet FP32':>20} | {'MobileFaceNet INT8':>20}")
        print("  " + "-" * 78)
        print(f"  {'ขนาดไฟล์โมเดล (MB)':<32} | {r_fp32['size_mb']:>17.2f} MB | {r_int8['size_mb']:>17.2f} MB")
        print(f"  {'เวลาสกัดเวกเตอร์เฉลี่ย (ms)':<32} | {r_fp32['avg_latency_ms']:>17.2f} ms | {r_int8['avg_latency_ms']:>17.2f} ms")
        print(f"  {'Accuracy (ความแม่นยำรวม %)':<32} | {r_fp32['metrics']['accuracy']:>19.2f}% | {r_int8['metrics']['accuracy']:>19.2f}%")
        print(f"  {'Precision (%)':<32} | {r_fp32['metrics']['precision']:>19.2f}% | {r_int8['metrics']['precision']:>19.2f}%")
        print(f"  {'Recall (%)':<32} | {r_fp32['metrics']['recall']:>19.2f}% | {r_int8['metrics']['recall']:>19.2f}%")
        print(f"  {'FAR (อัตรารับคนผิด %)':<32} | {r_fp32['metrics']['far']:>19.2f}% | {r_int8['metrics']['far']:>19.2f}%")
        print(f"  {'FRR (อัตราปฏิเสธคนถูก %)':<32} | {r_fp32['metrics']['frr']:>19.2f}% | {r_int8['metrics']['frr']:>19.2f}%")
        print("=" * 82)

    if export_md:
        md_path = os.path.join(SCRIPT_DIR, "accuracy_eval_report.md")
        with open(md_path, "w", encoding="utf-8") as f:
            f.write("# รายงานผลการประเมินความแม่นยำ (Confusion Matrix Evaluation Report)\n\n")
            f.write(f"- **จำนวนบุคคลทดสอบ:** {total_persons} บุคคล ({', '.join(dataset.keys())})\n")
            f.write(f"- **จำนวนภาพต้นฉบับ:** {total_raw_imgs} ภาพ (Cross-Image Verification + Field Augmentation)\n")
            f.write(f"- **Threshold ที่ใช้ประเมิน:** `{threshold:.2f}`\n\n")
            for res in results:
                m = res["metrics"]
                f.write(f"## {res['label']} (`{os.path.basename(res['model_path'])}`, {res['size_mb']:.2f} MB)\n\n")
                f.write("| Actual \\ Predicted | Predicted: MATCH | Predicted: UNKNOWN |\n")
                f.write("| :--- | :---: | :---: |\n")
                f.write(f"| **Actual: SAME PERSON** | **TP = {m['tp']}** | FN = {m['fn']} |\n")
                f.write(f"| **Actual: DIFFERENT** | FP = {m['fp']} | **TN = {m['tn']}** |\n\n")
                f.write(f"- **Accuracy:** **{m['accuracy']:.2f}%** | **Precision:** {m['precision']:.2f}% | **Recall:** {m['recall']:.2f}% | **F1:** {m['f1']:.2f}%\n")
                f.write(f"- **FAR:** {m['far']:.2f}% | **FRR:** {m['frr']:.2f}% | **Avg Latency:** {res['avg_latency_ms']:.2f} ms\n\n")
                f.write("### Threshold Sweep\n\n")
                f.write("| Threshold | Accuracy | Precision | Recall | FAR | FRR |\n")
                f.write("| :---: | :---: | :---: | :---: | :---: | :---: |\n")
                for thr, sm in res["sweep"].items():
                    f.write(f"| `{thr:.2f}` | {sm['accuracy']:.2f}% | {sm['precision']:.2f}% | {sm['recall']:.2f}% | {sm['far']:.2f}% | {sm['frr']:.2f}% |\n")
                f.write("\n")
        print(f"\n[OK] บันทึกรายงาน Markdown พร้อมตารางสำหรับเล่มโครงงานไว้ที่: {md_path}\n")


def main():
    parser = argparse.ArgumentParser(description="Confusion Matrix Accuracy Evaluation (FP32 vs INT8)")
    parser.add_argument("--data-dir", type=str, default=os.path.join(SCRIPT_DIR, "my_face_project", "known_faces"),
                        help="โฟลเดอร์ชุดข้อมูลทดสอบที่มีโฟลเดอร์ย่อยแยกตามชื่อคน")
    parser.add_argument("--threshold", type=float, default=0.50,
                        help="ค่า Cosine Similarity Threshold สำหรับตัดสินผล (default: 0.50)")
    parser.add_argument("--no-augment", action="store_true",
                        help="ทดสอบเฉพาะภาพดิบโดยไม่จำลองแสงและมุมหน้าเพิ่ม")
    parser.add_argument("--no-export", action="store_true",
                        help="ไม่ต้องสร้างไฟล์ accuracy_eval_report.md")
    args = parser.parse_args()

    detector = MTCNNDetector(image_size=112)
    dataset = load_person_folders(args.data_dir, detector)
    if len(dataset) < 2:
        print(f"[ERROR] ต้องมีโฟลเดอร์บุคคลอย่างน้อย 2 คนขึ้นไปใน: {args.data_dir}")
        sys.exit(1)

    fp32_path = os.path.join(SCRIPT_DIR, "mobilefacenet.onnx")
    int8_path = os.path.join(SCRIPT_DIR, "mobilefacenet_int8.onnx")

    results = []
    if os.path.exists(fp32_path):
        results.append(run_evaluation_for_model(
            "MobileFaceNet FP32 (Baseline)", fp32_path, dataset,
            threshold=args.threshold, use_augmentation=not args.no_augment
        ))
    if os.path.exists(int8_path):
        results.append(run_evaluation_for_model(
            "MobileFaceNet INT8 (Quantized)", int8_path, dataset,
            threshold=args.threshold, use_augmentation=not args.no_augment
        ))

    print_report(results, threshold=args.threshold, dataset=dataset, export_md=not args.no_export)


if __name__ == "__main__":
    main()
