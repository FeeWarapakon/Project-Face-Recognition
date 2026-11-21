# enroll_faces.py
# Proxy to 2_enroll_faces.py
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from importlib import import_module
_mod = import_module("2_enroll_faces")

FaceEnrollmentApp = _mod.FaceEnrollmentApp

if __name__ == "__main__":
    _mod.main()
