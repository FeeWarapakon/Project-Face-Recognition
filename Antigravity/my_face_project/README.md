MobileFaceNet face recognition demo for Orange Pi

Structure:

```
my_face_project/
├── models/
│   └── mobilefacenet.onnx
├── known_faces/
│   ├── somchai.jpg
│   └── john.jpg
└── main.py
```

Quick start on Orange Pi:

```bash
sudo apt update
sudo apt install -y python3-opencv python3-pip
pip3 install onnxruntime mediapipe numpy
mkdir -p models known_faces
wget -O models/mobilefacenet.onnx https://github.com/onnx/models/raw/main/validated/vision/body_analysis/arcface/models/arcface_w600k_mbf.onnx
python3 main.py
```

Notes:
- Put one image per person in `known_faces/` (or multiple images for better accuracy).
- If `mediapipe` wheel isn't available for your board, consider using an alternative face detector (OpenCV DNN or BlazeFace builds).
