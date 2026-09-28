#!/usr/bin/env bash
# Regenerates docs/media/panel_demo.gif and docs/media/panel_demo.mp4.
#
# It runs the operator panel's on-demand test `demoFrames`, which drives the real
# window through the demo (scan, alarm, clear, Online-Remote and back, an Abort)
# on Qt's offscreen platform and saves the frames, then assembles them into a GIF
# and, if ffmpeg is available, an MP4 of the same session at higher quality. The
# frames are rendered by the real widgets; this is NOT a screen recording.
#
#   scripts/make_demo_gif.sh [QT_BUILD_DIR] [PYTHON_WITH_PILLOW]
# Needs a Qt build (cmake --preset qt) and `pip install pillow`. The MP4 step is
# skipped, not fatal, if ffmpeg is not on PATH.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:-$here/build-qt}"
python="${2:-python3}"
frames="$(mktemp -d)"

cmake --build "$build" --target ssim_qt_panel_smoke_test -j2
# codesign: a binary built inside a sandboxed shell can end up with an ad-hoc
# signature the kernel later rejects (SIGKILL, "Code Signature Invalid");
# re-signing before running is cheap insurance and harmless when not needed.
codesign --force --sign - "$build/tests/ssim_qt_panel_smoke_test" 2>/dev/null || true
SSIM_FRAMES_DIR="$frames" QT_QPA_PLATFORM=offscreen \
    "$build/tests/ssim_qt_panel_smoke_test" demoFrames
"$python" "$here/scripts/make_demo_gif.py" "$frames" "$here/docs/media/panel_demo.gif"
if command -v ffmpeg >/dev/null 2>&1; then
    ffmpeg -y -framerate 8.333 -i "$frames/frame_%04d.png" \
        -vf "scale=960:-2,fps=20" -pix_fmt yuv420p -c:v libx264 -crf 23 \
        "$here/docs/media/panel_demo.mp4"
else
    echo "make_demo_gif.sh: ffmpeg not found, skipped panel_demo.mp4" >&2
fi
rm -rf "$frames"
