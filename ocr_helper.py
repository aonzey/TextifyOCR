#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
OCR helper script for TextifyOCR.
Usage: python ocr_helper.py <image_path> [engine]
  engine: "" / "builtin" -> built-in RapidOCR onnxruntime (rapid_ocr/)
          otherwise      -> plugin folder name under plugins/, e.g.
                            "RapidOCR-json", "TesseractOCR_fast",
                            "WechatOCR_plugin_full"
Prints recognized text to stdout (UTF-8).
"""

import sys
import os
import glob
import importlib.util


def find_models(base_dir):
    """Find ONNX model files in the rapid_ocr directory."""
    det_model = None
    rec_model = None
    cls_model = None

    # Detection models (prefer v4)
    det_candidates = glob.glob(os.path.join(base_dir, '*det*.onnx'))
    if det_candidates:
        for c in det_candidates:
            if 'v4' in c.lower():
                det_model = c
                break
        if not det_model:
            det_model = det_candidates[0]

    # Recognition models (prefer v4)
    rec_candidates = glob.glob(os.path.join(base_dir, '*rec*.onnx'))
    if rec_candidates:
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


def get_script_dir():
    # When frozen by PyInstaller, use the directory of the exe itself so
    # "rapid_ocr" and "plugins" are looked up next to ocr_helper.exe.
    if getattr(sys, 'frozen', False):
        return os.path.dirname(os.path.abspath(sys.executable))
    return os.path.dirname(os.path.abspath(__file__))


def find_resource_dir(name, script_dir):
    """Return the first existing "<script_dir>/<name>" or
    "<script_dir>/../<name>" (caller picks one)."""
    candidates = [
        os.path.join(script_dir, name),
        os.path.normpath(os.path.join(script_dir, "..", name)),
    ]
    for c in candidates:
        if os.path.isdir(c):
            return c
    return candidates[0]


def run_builtin(image_path, script_dir):
    """Built-in RapidOCR onnxruntime engine (rapid_ocr/ models)."""
    model_dir = find_resource_dir("rapid_ocr", script_dir)

    if not os.path.isdir(model_dir):
        print("Error: rapid_ocr directory not found: " + model_dir, file=sys.stderr)
        sys.exit(1)

    det_model, rec_model, cls_model = find_models(model_dir)

    if not det_model or not rec_model:
        print("Error: Required ONNX models not found in " + model_dir, file=sys.stderr)
        sys.exit(1)

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

    print("\n".join(lines))


def extract_defaults(options, prefix="", out=None):
    """Build a default argd dict from Umi-OCR style option specs.

    Nested groups ("type": "group") are flattened with dotted keys, e.g.
    "language.chi_sim", matching what plugins expect in argd.
    """
    if out is None:
        out = {}
    for key, spec in options.items():
        if key in ("title", "type", "toolTip", "enabledFold", "fold"):
            continue
        if not isinstance(spec, dict):
            continue
        full_key = prefix + key
        if spec.get("type") == "group":
            extract_defaults(spec, prefix=full_key + ".", out=out)
            continue
        if "default" in spec:
            out[full_key] = spec["default"]
        elif "optionsList" in spec and spec["optionsList"]:
            out[full_key] = spec["optionsList"][0][0]
        elif "min" in spec:
            out[full_key] = spec["min"]
        else:
            out[full_key] = False
    return out


def load_plugin(plugin_dir):
    """Load a Umi-OCR plugin package by path and return its PluginInfo dict."""
    init_path = os.path.join(plugin_dir, "__init__.py")
    if not os.path.isfile(init_path):
        raise FileNotFoundError("Plugin entry not found: " + init_path)

    # Make the shim plugin_i18n (and nothing else) importable from the
    # plugins directory.
    plugins_root = os.path.dirname(plugin_dir)
    if plugins_root not in sys.path:
        sys.path.insert(0, plugins_root)

    spec = importlib.util.spec_from_file_location(
        "_textify_ocr_plugin", init_path, submodule_search_locations=[plugin_dir])
    mod = importlib.util.module_from_spec(spec)
    sys.modules["_textify_ocr_plugin"] = mod
    spec.loader.exec_module(mod)

    info = getattr(mod, "PluginInfo", None)
    if not info or info.get("group") != "ocr" or "api_class" not in info:
        raise ValueError("Not a valid OCR plugin: " + plugin_dir)
    return info


def join_fragments(datas):
    """Join OCR fragments into readable text.

    Fragments carry an "end" separator, but plugins like TesseractOCR merge
    different lines with a plain space. Use the bounding boxes to restore
    line breaks: when a fragment's top edge is well below the previous
    fragment's top edge, start a new line.
    """
    parts = []
    prev_top = None
    prev_height = 0
    for d in datas:
        if not isinstance(d, dict):
            continue
        text = d.get("text", "")
        if not text or text.isspace():
            continue

        top = None
        height = 0
        box = d.get("box")
        try:
            ys = [p[1] for p in box]
            top = min(ys)
            height = max(ys) - min(ys)
        except Exception:
            top = None

        if parts:
            end = d.get("end")
            if top is not None and prev_top is not None and prev_height > 0 \
                    and (top - prev_top) > prev_height * 0.6:
                parts.append("\n")
            elif end:
                parts.append(end)
            else:
                parts.append(" ")
        parts.append(text)

        if top is not None:
            prev_top = top
            prev_height = max(height, 1)

    return "".join(parts)


def run_plugin(image_path, engine, script_dir):
    """Run a Umi-OCR style OCR plugin from plugins/<engine>."""
    import io
    import contextlib

    plugins_dir = find_resource_dir("plugins", script_dir)
    plugin_dir = os.path.join(plugins_dir, engine)
    if not os.path.isdir(plugin_dir):
        print("Error: OCR plugin not found: " + plugin_dir, file=sys.stderr)
        sys.exit(1)

    # Plugins resolve paths from their own subprocess working directory,
    # so always hand them an absolute image path.
    image_path = os.path.abspath(image_path)

    info = load_plugin(plugin_dir)

    global_argd = extract_defaults(info.get("global_options") or info.get("globalOptions") or {})
    local_argd = extract_defaults(info.get("local_options") or info.get("localOptions") or {})

    # Usability tweak: if a plugin exposes selectable languages (TesseractOCR)
    # and ships a Simplified Chinese model, enable it alongside the default
    # language, since Chinese + English is the common use case.
    if "language.chi_sim" in local_argd and not local_argd["language.chi_sim"]:
        local_argd["language.chi_sim"] = True

    # Plugins print diagnostics to stdout; keep stdout clean so only the
    # recognized text reaches the C++ side.
    real_stdout = sys.stdout
    result_text = None
    error = None
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            api = info["api_class"](global_argd)

            err = api.start(local_argd)
            if err:
                raise RuntimeError("OCR plugin start failed: " + str(err))

            try:
                res = api.runPath(image_path)
            finally:
                stop = getattr(api, "stop", None)
                if callable(stop):
                    try:
                        stop()
                    except Exception:
                        pass

        if not isinstance(res, dict):
            raise RuntimeError("OCR plugin returned: " + str(res))

        if res.get("code") == 101:
            result_text = ""  # No text recognized
        elif res.get("code") != 100:
            raise RuntimeError("OCR plugin returned: " + str(res))
        else:
            result_text = join_fragments(res.get("data") or [])
    except SystemExit:
        raise
    except Exception as e:
        error = e
    finally:
        sys.stdout = real_stdout

    if error is not None:
        print("Error: " + str(error), file=sys.stderr)
        sys.exit(1)

    print(result_text.rstrip("\n"))


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
        print("Usage: python ocr_helper.py <image_path> [engine]", file=sys.stderr)
        sys.exit(1)

    image_path = sys.argv[1]
    engine = sys.argv[2] if len(sys.argv) > 2 else ""

    if not os.path.exists(image_path):
        print("Error: Image file not found: " + image_path, file=sys.stderr)
        sys.exit(1)

    script_dir = get_script_dir()

    try:
        if engine in ("", "builtin"):
            run_builtin(image_path, script_dir)
        else:
            run_plugin(image_path, engine, script_dir)
    except SystemExit:
        raise
    except Exception as e:
        print("Error: " + str(e), file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
