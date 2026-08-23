#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
OCR helper script for Textify.
Usage: python ocr_helper.py <image_path>
Prints recognized text to stdout.
"""

import sys
import os
import glob

def find_models(base_dir):
    """Find ONNX model files in the rapid_ocr directory."""
    # Look for detection model
    det_model = None
    rec_model = None
    cls_model = None

    # Detection models (prefer v4)
    det_candidates = glob.glob(os.path.join(base_dir, '*det*.onnx'))
    if det_candidates:
        # Prefer PP-OCRv4 det
        for c in det_candidates:
            if 'v4' in c.lower():
                det_model = c
                break
        if not det_model:
            det_model = det_candidates[0]

    # Recognition models (prefer v4)
    rec_candidates = glob.glob(os.path.join(base_dir, '*rec*.onnx'))
    if rec_candidates:
        # Prefer PP-OCRv4 rec
        for c in rec_candidates:
            if 'v4' in c.lower():
                rec_model = c
                break
        if not rec_model:
            rec_model = rec_candidates[0]

    # Classification model
    cls_candidates = glob.glob(os.path.join(base_dir, '*cls*.onnx'))
    if cls_candidates:
        cls_model = cls_candidates[0]

    return det_model, rec_model, cls_model

def main():
    # Force UTF-8 output. When stdout is a pipe (as with Textify), Python
    # would otherwise use the ANSI code page (e.g. GBK on Chinese Windows),
    # which garbles the Chinese text on the C++ side that decodes UTF-8.
    try:
        sys.stdout.reconfigure(encoding='utf-8')
        sys.stderr.reconfigure(encoding='utf-8')
    except Exception:
        pass

    if len(sys.argv) < 2:
        print("Usage: python ocr_helper.py <image_path>", file=sys.stderr)
        sys.exit(1)

    image_path = sys.argv[1]
    if not os.path.exists(image_path):
        print("Error: Image file not found: " + image_path, file=sys.stderr)
        sys.exit(1)

    # Determine the script directory to find models.
    # When frozen by PyInstaller, use the directory of the exe itself so
    # "rapid_ocr" is looked up next to ocr_helper.exe.
    if getattr(sys, 'frozen', False):
        script_dir = os.path.dirname(os.path.abspath(sys.executable))
    else:
        script_dir = os.path.dirname(os.path.abspath(__file__))
    model_dir = os.path.join(script_dir, "rapid_ocr")

    if not os.path.isdir(model_dir):
        print("Error: rapid_ocr directory not found: " + model_dir, file=sys.stderr)
        sys.exit(1)

    det_model, rec_model, cls_model = find_models(model_dir)

    if not det_model or not rec_model:
        print("Error: Required ONNX models not found in " + model_dir, file=sys.stderr)
        sys.exit(1)

    try:
        from rapidocr_onnxruntime import RapidOCR

        kwargs = {
            'det_model_path': det_model,
            'rec_model_path': rec_model,
        }
        if cls_model:
            kwargs['cls_model_path'] = cls_model

        engine = RapidOCR(**kwargs)

        result, elapse = engine(image_path)

        if result is None or len(result) == 0:
            print("")
            return

        # result is a list of [box, text, score]
        lines = []
        for item in result:
            if item and len(item) >= 2 and item[1]:
                lines.append(item[1])

        # Print all recognized text, one line per text region
        print("\n".join(lines))

    except Exception as e:
        print("Error: " + str(e), file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
