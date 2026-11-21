# realtime_recognition.py
# Proxy to 3_realtime_recognition.py
import os
import sys

# Import everything from 3_realtime_recognition
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from importlib import import_module
_mod = import_module("3_realtime_recognition")

RealTimeFaceRecognitionApp = _mod.RealTimeFaceRecognitionApp

if __name__ == "__main__":
    _mod.main()
