#!/usr/bin/env python3
"""Assembles the README demo GIF from the numbered frames the panel test records.

    python3 scripts/make_demo_gif.py FRAMES_DIR OUTPUT.gif [--width 640]

The frames are rendered offscreen by the real panel widgets while a scripted
session drives them (see scripts/make_demo_gif.sh). They are not a screen
recording. One shared colour palette is used for all frames, because a palette
per frame makes GIFs flicker.
"""
import argparse
import glob
import os
import sys

from PIL import Image


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("frames_dir")
    ap.add_argument("output")
    ap.add_argument("--width", type=int, default=640)
    ap.add_argument("--colors", type=int, default=256)
    ap.add_argument("--ms", type=int, default=120, help="time each frame is shown")
    ap.add_argument("--hold-last-ms", type=int, default=2500)
    args = ap.parse_args()

    paths = sorted(glob.glob(os.path.join(args.frames_dir, "frame_*.png")))
    if len(paths) < 2:
        sys.exit(f"no frames found in {args.frames_dir}")

    def load(path):
        image = Image.open(path).convert("RGB")
        height = round(image.height * args.width / image.width)
        return image.resize((args.width, height), Image.LANCZOS)

    frames = [load(p) for p in paths]

    # One palette for the whole recording. It is built from frames spread over
    # the recording plus the most colourful ones (the wafer map), otherwise the
    # smooth map colours are missing from the palette and come out as bands.
    def colourfulness(image):
        small = image.resize((80, max(1, round(image.height * 80 / image.width))))
        data = small.tobytes()
        return sum(1 for i in range(0, len(data), 3)
                   if max(data[i:i + 3]) - min(data[i:i + 3]) > 60)

    step = max(1, len(frames) // 16)
    chosen = set(range(0, len(frames), step))
    chosen.update(sorted(range(len(frames)), key=lambda i: colourfulness(frames[i]))[-6:])
    sample = [frames[i] for i in sorted(chosen)]
    sheet = Image.new("RGB", (args.width, frames[0].height * len(sample)))
    for i, image in enumerate(sample):
        sheet.paste(image, (0, i * frames[0].height))
    palette = sheet.quantize(colors=args.colors, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)

    quantized = [f.quantize(palette=palette, dither=Image.Dither.NONE) for f in frames]
    durations = [args.ms] * len(quantized)
    durations[-1] = args.hold_last_ms

    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    quantized[0].save(args.output, save_all=True, append_images=quantized[1:],
                      duration=durations, loop=0, optimize=True, disposal=1)
    size = os.path.getsize(args.output)
    print(f"{args.output}: {len(quantized)} frames, {args.width}px wide, {size / 1e6:.2f} MB")


if __name__ == "__main__":
    main()
