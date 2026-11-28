"""Reproduce the Di Lac diagnostic jobs without confirming a machine offset."""

import argparse
import json
from pathlib import Path
import struct
import subprocess
import time


def read_length(stl):
    with stl.open("rb") as source:
        source.seek(80)
        count = struct.unpack("<I", source.read(4))[0]
        if not count or stl.stat().st_size != 84 + 50 * count:
            raise ValueError("Cần STL nhị phân có bảng tam giác đầy đủ.")
        low, high = float("inf"), float("-inf")
        for _, _, _, x0, _, _, x1, _, _, x2, _, _, _ in struct.iter_unpack(
            "<12fH", source.read()
        ):
            low = min(low, x0, x1, x2)
            high = max(high, x0, x1, x2)
    if not high > low:
        raise ValueError("Mẫu không có chiều dài X.")
    return count, low, high


def parameters(args, low, high):
    scale = 250 / (high - low)
    rotary = [
        {"role": "table", "direction": [1, 0, 0], "origin": [0, 0, 0], "sign": 1},
        {"role": "head", "direction": [0, 1, 0], "origin": [0, 0, 0], "sign": 1},
    ]
    machine = {
        "workOffsetMachine": [264.607, 455.104, -470.090],
        "offsetConfirmed": False,
        "swingarmMm": 232.918,
        "rtcpMode": "G43.4",
        "rtcpRegister": "H1",
        "controllerMode": "7",
        "limits": {"Z": [-450, 200], "B": [-20, 20]},
        "rotary": rotary,
    }
    tools = [
        dict(diameter=12.7, tipRadius=0, flute=24, overhang=85, holderDiameter=24),
        dict(diameter=10, tipRadius=5, flute=22, overhang=70, holderDiameter=24),
        dict(diameter=8, tipRadius=.4, singleSideHalfAngle=10, flute=22, overhang=80,
             holderDiameter=20),
    ]
    rough = [
        dict(name=f"Phá mặt {angle}°", active=True, axes=4, toolIndex=0,
             faceAngle=angle, step=3, stepDown=3, allowance=2, feed=550, rpm=16000)
        for angle in (0, 180, 90, 270)
    ]
    semifinish = dict(
        name="Phá tròn cận tinh", active=True, axes=args.axes, toolIndex=1,
        faceAngle=-1, step=2.5, allowance=1.2, bMin=-20, bMax=20, feed=700, rpm=16000,
    )
    finish = dict(
        name="Chạy tinh R0.4", active=True, axes=args.axes, toolIndex=2,
        faceAngle=-1, step=args.pitch, allowance=0, bMin=-20, bMax=20, feed=600, rpm=18000,
        pointSpacing=args.point_spacing, lead=args.lead, lean=args.lean,
    )
    if args.stage == "strip":
        finish.update(clipMin=120, clipMax=125)
        paths = [finish]
    elif args.stage == "raster":
        paths = rough
    else:
        paths = rough + [semifinish, finish]
    if args.clip:
        for path in paths:
            path.update(clipMin=args.clip[0], clipMax=args.clip[1])
    return {
        "operation": "wrap-stl" if args.stage == "surface" else "plan-motion",
        "file": str(args.stl.resolve()),
        "transformColumnMajor": [scale,0,0,0, 0,1,0,0, 0,0,1,0, -low*scale,0,0,1],
        "machine": machine,
        "stock": {"shape": "box", "min": [0, -50, -40], "max": [250, 50, 40]},
        "retract": (50**2 + 40**2)**.5 + 50,
        "workflow": {
            "axes": args.axes, "post": "cm600" if args.axes == 5 else "shanlong4",
            "faces": 4, "wrapColumns": 24, "wrapStacks": 32, "tools": tools, "paths": paths,
        },
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker", type=Path, required=True)
    parser.add_argument("--stl", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--axes", type=int, choices=(4, 5), default=4)
    parser.add_argument("--stage", choices=("surface", "raster", "strip", "full"), required=True)
    parser.add_argument("--pitch", type=float, default=.22)
    parser.add_argument("--point-spacing", type=float, default=0.0)
    parser.add_argument("--lead", type=float, default=0.0)
    parser.add_argument("--lean", type=float, default=0.0)
    parser.add_argument("--clip", type=float, nargs=2, metavar=("MIN", "MAX"))
    parser.add_argument("--require-verified", action="store_true")
    args = parser.parse_args()
    if not .01 <= args.pitch <= 10 or not 0 <= args.point_spacing <= 10 or not abs(args.lead) <= 60 or not abs(args.lean) <= 60:
        parser.error("Khoảng cách điểm hoặc góc Lead/Lean không hợp lệ.")
    if args.clip and not 0 <= args.clip[0] < args.clip[1] <= 250:
        parser.error("Giới hạn lát thử phải nằm trong khoảng X 0–250 mm.")
    triangles, low, high = read_length(args.stl)
    args.out.mkdir(parents=True, exist_ok=True)
    name = args.out / f"{args.stage}-{args.axes}x"
    request = json.dumps(parameters(args, low, high), ensure_ascii=False)
    name.with_suffix(".request.json").write_text(request + "\n", encoding="utf-8")
    start = time.monotonic()
    with name.with_suffix(".progress.jsonl").open("wb") as progress:
        with name.with_suffix(".result.json").open("wb") as output:
            completed = subprocess.run(
                [str(args.worker.resolve())], input=(request + "\n").encode("utf-8"),
                stdout=output, stderr=progress, check=False,
            )
    result = json.loads(name.with_suffix(".result.json").read_text(encoding="utf-8"))
    summary = {
        key: result[key] for key in (
            "ok", "verified", "blocks", "unreachable", "liftCount", "error",
            "calibratedOffset", "diagnostics",
        ) if key in result
    }
    summary.update(
        seconds=round(time.monotonic() - start, 2), triangles=triangles,
        originalLength=high-low, displayedVertices=len(result.get("surface", [])),
        playbackFrames=len(result.get("playback", [])),
        stages=[{k: s[k] for k in ("group", "gouge", "remaining")} for s in result.get("stages", [])],
    )
    serialized = json.dumps(summary, indent=2, ensure_ascii=False)
    name.with_suffix(".summary.json").write_text(serialized + "\n", encoding="utf-8")
    print(serialized)
    if args.require_verified and args.stage != "surface" and not result.get("verified", False):
        return completed.returncode or 2
    return completed.returncode


if __name__ == "__main__":
    raise SystemExit(main())
