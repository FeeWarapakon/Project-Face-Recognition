# รายงานผลการประเมินความแม่นยำ (Confusion Matrix Evaluation Report)

- **จำนวนบุคคลทดสอบ:** 3 บุคคล (Narawich Anuwong, Sunya Lungjay, Warapakon Phimwong)
- **จำนวนภาพต้นฉบับ:** 9 ภาพ (Cross-Image Verification + Field Augmentation)
- **Threshold ที่ใช้ประเมิน:** `0.50`

## MobileFaceNet FP32 (Baseline) (`mobilefacenet.onnx`, 3.81 MB)

| Actual \ Predicted | Predicted: MATCH | Predicted: UNKNOWN |
| :--- | :---: | :---: |
| **Actual: SAME PERSON** | **TP = 345** | FN = 96 |
| **Actual: DIFFERENT** | FP = 0 | **TN = 1323** |

- **Accuracy:** **94.56%** | **Precision:** 100.00% | **Recall:** 78.23% | **F1:** 87.79%
- **FAR:** 0.00% | **FRR:** 21.77% | **Avg Latency:** 2.34 ms

### Threshold Sweep

| Threshold | Accuracy | Precision | Recall | FAR | FRR |
| :---: | :---: | :---: | :---: | :---: | :---: |
| `0.35` | 99.72% | 100.00% | 98.87% | 0.00% | 1.13% |
| `0.40` | 99.49% | 100.00% | 97.96% | 0.00% | 2.04% |
| `0.45` | 98.64% | 100.00% | 94.56% | 0.00% | 5.44% |
| `0.50` | 94.56% | 100.00% | 78.23% | 0.00% | 21.77% |
| `0.55` | 92.80% | 100.00% | 71.20% | 0.00% | 28.80% |
| `0.60` | 88.32% | 100.00% | 53.29% | 0.00% | 46.71% |
| `0.65` | 84.52% | 100.00% | 38.10% | 0.00% | 61.90% |
| `0.70` | 81.24% | 100.00% | 24.94% | 0.00% | 75.06% |

## MobileFaceNet INT8 (Quantized) (`mobilefacenet_int8.onnx`, 1.20 MB)

| Actual \ Predicted | Predicted: MATCH | Predicted: UNKNOWN |
| :--- | :---: | :---: |
| **Actual: SAME PERSON** | **TP = 345** | FN = 96 |
| **Actual: DIFFERENT** | FP = 0 | **TN = 1323** |

- **Accuracy:** **94.56%** | **Precision:** 100.00% | **Recall:** 78.23% | **F1:** 87.79%
- **FAR:** 0.00% | **FRR:** 21.77% | **Avg Latency:** 2.55 ms

### Threshold Sweep

| Threshold | Accuracy | Precision | Recall | FAR | FRR |
| :---: | :---: | :---: | :---: | :---: | :---: |
| `0.35` | 99.66% | 100.00% | 98.64% | 0.00% | 1.36% |
| `0.40` | 99.49% | 100.00% | 97.96% | 0.00% | 2.04% |
| `0.45` | 98.58% | 100.00% | 94.33% | 0.00% | 5.67% |
| `0.50` | 94.56% | 100.00% | 78.23% | 0.00% | 21.77% |
| `0.55` | 92.74% | 100.00% | 70.98% | 0.00% | 29.02% |
| `0.60` | 88.27% | 100.00% | 53.06% | 0.00% | 46.94% |
| `0.65` | 84.24% | 100.00% | 36.96% | 0.00% | 63.04% |
| `0.70` | 80.73% | 100.00% | 22.90% | 0.00% | 77.10% |

