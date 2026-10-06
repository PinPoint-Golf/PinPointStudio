#!/usr/bin/env python3
"""convert_fp16 — the ViTPose-B WholeBody model in float16, once, beside the fp32 file.

docs/design/pose_inference_performance_plan.md step 3. The offline pose pass reads
`pose.modelPrecision` ("fp32" | "fp16", default fp32): "fp16" makes PoseEstimatorViTPose
load `<model>.fp16.onnx` from the SAME directory as the fp32 model when that file exists,
and log a fallback to fp32 when it does not. This script makes that file. It is NOT wired
into CMake: whether the fp16 model is packaged (180 MB beside the 360 MB fp32) is Mark's
call after the keypoint gate.

The graph keeps float32 inputs and outputs (keep_io_types=True), so the C++ side feeds the
same [B,3,256,192] float32 tensor and reads the same [B,133,64,48] float32 heatmaps — the
casts sit at the graph's edges.

The ViT blocks normalise with a decomposed LayerNorm (opset 11: ReduceMean, Sub, Pow(·,2),
Sqrt, Div). Those statistics stay in float32 BY DEFAULT: converted whole, the graph does
not load in ORT 1.26 at all — its SimplifiedLayerNormFusion trips over a cast the converter
inserts on the Pow exponent all twelve blocks share ("Attempting to get index by a name which
does not exist: InsertedPrecisionFreeCast_/backbone/blocks.11/norm1/Constant_output_0",
measured 6 Oct 2026). Keeping Pow/ReduceMean/Sqrt in float32 avoids the pattern and also keeps
x² clear of float16's 65504 ceiling. --convert-layernorm converts them anyway. The check
below runs a random input through both models on onnxruntime's CPU EP and prints the heatmap
error and the argmax agreement (6 Oct: max |Δ| 0.0018 on a 0.38 peak, 98.5 % of argmaxes).

One-off setup (a venv UNDER build/, never outside the repo):

    /usr/bin/python3 -m venv build/.venv-pose
    build/.venv-pose/bin/pip install onnx onnxconverter_common numpy onnxruntime

Convert (macOS tree shown; on Windows the model sits beside the exe in models/):

    build/.venv-pose/bin/python tools/pose/convert_fp16.py \
        build/tools-parity-ninja/_deps/vitpose/vitpose-b-wholebody.onnx
    # → build/tools-parity-ninja/_deps/vitpose/vitpose-b-wholebody.fp16.onnx

Where the app looks: pinpoint::modelFilePath(VITPOSE_MODEL_FILE, "vitpose") resolves the
fp32 file (the bundle's Resources/models/vitpose, or models/ beside a bare exe, or the
build tree's _deps/vitpose); the fp16 file must sit in that same directory.
"""
import argparse
import os
import sys
import warnings

import numpy as np
import onnx
from onnxconverter_common import float16


def convert(src, dst, block_layernorm):
    model = onnx.load(src)
    # ~100 weights below fp16's 1e-7 floor are clamped to ±1e-7 — one warning each, noise.
    warnings.filterwarnings("ignore", message="the float32 number")
    block = None
    if block_layernorm:
        block = list(float16.DEFAULT_OP_BLOCK_LIST) + ["Pow", "ReduceMean", "Sqrt"]
    m16 = float16.convert_float_to_float16(model, keep_io_types=True,
                                           op_block_list=block)
    onnx.save(m16, dst)
    print(f"wrote {dst} ({os.path.getsize(dst) / 1e6:.0f} MB)")


def check(src, dst, batch=2):
    try:
        import onnxruntime as ort
    except ImportError:
        print("onnxruntime not installed — skipping the numeric check")
        return
    rng = np.random.default_rng(0)
    x = rng.standard_normal((batch, 3, 256, 192)).astype(np.float32)
    outs = []
    for path in (src, dst):
        s = ort.InferenceSession(path, providers=["CPUExecutionProvider"])
        outs.append(s.run(None, {s.get_inputs()[0].name: x})[0])
    a, b = outs
    if not np.isfinite(b).all():
        print("fp16 output has inf/NaN")
        sys.exit(1)
    err = np.abs(a - b)
    peak = np.abs(a).max()
    am = a.reshape(batch, a.shape[1], -1).argmax(-1)
    bm = b.reshape(batch, b.shape[1], -1).argmax(-1)
    print(f"heatmap |fp32 − fp16|: max {err.max():.4g}, mean {err.mean():.3g} "
          f"(fp32 peak {peak:.3g}); argmax agreement {np.mean(am == bm) * 100:.1f} % "
          f"of {am.size} channels")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("model", help="the fp32 vitpose-*.onnx")
    ap.add_argument("--out", help="default: <model>.fp16.onnx beside it")
    ap.add_argument("--convert-layernorm", action="store_true",
                    help="convert the LayerNorm statistics too (does NOT load in ORT 1.26)")
    ap.add_argument("--no-check", action="store_true")
    args = ap.parse_args()
    src = args.model
    dst = args.out or (src[:-5] if src.endswith(".onnx") else src) + ".fp16.onnx"
    convert(src, dst, not args.convert_layernorm)
    if not args.no_check:
        check(src, dst)


if __name__ == "__main__":
    main()
