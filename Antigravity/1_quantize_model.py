# 1_quantize_model.py
# ==============================================================================
# โครงงาน: Face Recognition on Raspberry Pi 5 (MobileFaceNet + INT8 Quantization)
# หน้าที่: ทำ Static INT8 Quantization จากโมเดล FP32 ONNX
# ==============================================================================

import os
import sys
import glob
import cv2
import numpy as np
import onnx
import onnxruntime as ort
from onnxruntime.quantization import (  # type: ignore
    quantize_static,
    quantize_dynamic,
    CalibrationDataReader,
    QuantType,
    QuantFormat
)
from onnxruntime.quantization.shape_inference import quant_pre_process

if sys.stdout and hasattr(sys.stdout, 'reconfigure'):
    try:
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    except Exception:
        pass


class FaceCalibrationDataReader(CalibrationDataReader):
    """
    Calibration Data Reader สำหรับโหลดภาพใบหน้าตัวอย่าง (Calibration Dataset)
    ใช้สำหรับหาค่า Dynamic Range (Min/Max, Scale, Zero-point) ของ Activations
    เพื่อให้โมเดล INT8 ไม่สูญเสียความแม่นยำ
    """
    def __init__(self, model_path: str, image_dir: str = "calib_images"):
        super().__init__()
        
        # 1. ตรวจสอบชื่อ Input Tensor และขนาดมิติที่โมเดลต้องการโดยอัตโนมัติ
        self.input_name = "image_tensor"
        self.target_height = 224
        self.target_width = 224
        self.is_nchw = True

        try:
            session = ort.InferenceSession(model_path, providers=['CPUExecutionProvider'])
            input_meta = session.get_inputs()[0]
            self.input_name = input_meta.name
            shape = input_meta.shape
            
            # ตรวจสอบว่า Shape เป็น NCHW ([1, 3, H, W]) หรือ NHWC ([1, H, W, 3])
            if len(shape) == 4:
                if shape[1] == 3: # NCHW
                    self.is_nchw = True
                    self.target_height = int(shape[2]) if isinstance(shape[2], int) else 224
                    self.target_width = int(shape[3]) if isinstance(shape[3], int) else 224
                elif shape[3] == 3: # NHWC
                    self.is_nchw = False
                    self.target_height = int(shape[1]) if isinstance(shape[1], int) else 224
                    self.target_width = int(shape[2]) if isinstance(shape[2], int) else 224

            print(f"[*] ตรวจพบ Model Input: '{self.input_name}' | ขนาด: {self.target_width}x{self.target_height} | Format: {'NCHW' if self.is_nchw else 'NHWC'}")
        except Exception as e:
            print(f"[!] ใช้ค่า Input Defaults: '{self.input_name}' ({self.target_width}x{self.target_height}) [เหตุผล: {e}]")

        # 2. ค้นหาภาพสำหรับ Calibration
        self.image_paths = glob.glob(os.path.join(image_dir, "*.jpg")) + glob.glob(os.path.join(image_dir, "*.png"))
        if not self.image_paths:
            alt_dirs = [
                os.path.join("my_face_project", "known_faces"),
                os.path.join("my_face_project", "original_img"),
                "known_faces",
                "enrolled_snapshots"
            ]
            for alt_dir in alt_dirs:
                if os.path.exists(alt_dir):
                    found = glob.glob(os.path.join(alt_dir, "**", "*.jpg"), recursive=True) + \
                            glob.glob(os.path.join(alt_dir, "**", "*.png"), recursive=True)
                    if found:
                        self.image_paths = found
                        print(f"[*] พบภาพสำหรับ Calibration จากโฟลเดอร์ '{alt_dir}' จำนวน {len(found)} ภาพ")
                        break

        self.cursor = 0
        if not self.image_paths:
            print(f"[!] คำเตือน: ไม่พบไฟล์ภาพจริง จะใช้ Synthetic Random Data ในการ Calibrate")

    def _generate_synthetic_tensor(self):
        if self.is_nchw:
            tensor = np.random.uniform(-1.0, 1.0, (1, 3, self.target_height, self.target_width)).astype(np.float32)
        else:
            tensor = np.random.uniform(-1.0, 1.0, (1, self.target_height, self.target_width, 3)).astype(np.float32)
        return {self.input_name: tensor}

    def get_next(self):
        # หากไม่มีรูปจริง หรืออ่านภาพจนครบแล้ว ให้ใช้ Synthetic Data เล็กน้อย
        if self.cursor >= len(self.image_paths) or len(self.image_paths) == 0:
            if self.cursor < 30: # สร้าง sample 30 ชุด
                self.cursor += 1
                return self._generate_synthetic_tensor()
            return None

        img_path = self.image_paths[self.cursor]
        self.cursor += 1

        img = cv2.imread(img_path)
        if img is None:
            return self._generate_synthetic_tensor()

        img = cv2.resize(img, (self.target_width, self.target_height), interpolation=cv2.INTER_AREA)
        # Preprocessing: BGR -> RGB, Normalize [-1, 1]
        img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB).astype(np.float32)
        img = (img - 127.5) / 128.0

        if self.is_nchw:
            img = np.transpose(img, (2, 0, 1)) # HWC -> CHW
        img = np.expand_dims(img, axis=0) # [1, 3, H, W] หรือ [1, H, W, 3]

        return {self.input_name: img}


def quantize_mobilefacenet(
    input_model="mobilefacenet.onnx",
    output_model="mobilefacenet_int8.onnx",
    calib_dir="calib_images"
):
    print("=" * 60)
    print("  เริ่มต้นกระบวนการ Post-Training INT8 Quantization")
    print("=" * 60)

    # ตรวจสอบตำแหน่งไฟล์โมเดลต้นฉบับ
    if not os.path.exists(input_model):
        alt_path = os.path.join("my_face_project", "models", input_model)
        if os.path.exists(alt_path):
            input_model = alt_path
        else:
            print(f"[!] ไม่พบไฟล์โมเดลต้นฉบับ: {input_model}")
            print(f"    กรุณาตรวจสอบว่ามีไฟล์ mobilefacenet.onnx ใน my_face_project/models/ หรือไม่")
            return

    print(f"[*] โมเดลต้นทาง (FP32): {input_model}")

    # ดำเนินการ Pre-processing (Shape Inference & Optimization) ก่อน Quantize
    preprocessed_model = "mobilefacenet_preprocessed.onnx"
    model_to_quantize = input_model
    try:
        print("[*] กำลังทำ ONNX Shape Inference & Pre-processing...")
        quant_pre_process(input_model, preprocessed_model, auto_merge=True)
        if os.path.exists(preprocessed_model):
            model_to_quantize = preprocessed_model
            print("[✓] Pre-processing สำเร็จ")
    except Exception as e:
        print(f"[!] ข้ามขั้นตอน Pre-processing: {e}")
        model_to_quantize = input_model

    reader = FaceCalibrationDataReader(model_path=model_to_quantize, image_dir=calib_dir)

    print(f"[*] กำลังแปลงโมเดล {model_to_quantize} -> {output_model} ...")
    quantize_static(
        model_input=model_to_quantize,
        model_output=output_model,
        calibration_data_reader=reader,
        quant_format=QuantFormat.QDQ,     # แนะนำ QDQ format
        per_channel=True,                 # Quantize น้ำหนักแยก Channel เพื่อความแม่นยำสูง
        weight_type=QuantType.QInt8,      # น้ำหนักเป็น Signed INT8 (-128 ถึง 127)
        activation_type=QuantType.QUInt8  # Activations เป็น Unsigned INT8 (0 ถึง 255)
    )

    # ลบไฟล์ชั่วคราว
    if os.path.exists(preprocessed_model):
        try:
            os.remove(preprocessed_model)
        except Exception:
            pass

    size_fp32 = os.path.getsize(input_model) / (1024 * 1024)
    size_int8 = os.path.getsize(output_model) / (1024 * 1024)
    reduction = (1 - (size_int8 / size_fp32)) * 100

    print("\n[✓] การ Quantization สำเร็จเรียบร้อย!")
    print(f"    - ขนาดโมเดลเดิม (FP32) : {size_fp32:.2f} MB")
    print(f"    - ขนาดโมเดลใหม่ (INT8) : {size_int8:.2f} MB")
    print(f"    - บีบอัดเล็กลง          : {reduction:.1f}%")
    print(f"    - บันทึกไฟล์ที่        : {output_model}")

    import shutil
    alt_out_dir = os.path.join("my_face_project", "models")
    if os.path.exists(alt_out_dir):
        alt_out_file = os.path.join(alt_out_dir, os.path.basename(output_model))
        shutil.copy(output_model, alt_out_file)
        print(f"    - สำเนาไปยัง          : {alt_out_file}")
    print("=" * 60)


if __name__ == "__main__":
    quantize_mobilefacenet()
