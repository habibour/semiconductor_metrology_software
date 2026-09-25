#!/usr/bin/env bash
# Regenerates docs/media/panel_demo.gif.
#
# It runs the operator panel's on-demand test `demoFrames`, which drives the real
# window through the demo (scan, alarm, clear, Online-Remote) on Qt's offscreen
# platform and saves the frames, then assembles them into a GIF. The frames are
# rendered by the real widgets; this is NOT a screen recording.
#
#   scripts/make_demo_gif.sh [QT_BUILD_DIR] [PYTHON_WITH_PILLOW]
# Needs a Qt build (cmake --preset qt) and `pip install pillow`.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:-$here/build-qt}"
python="${2:-python3}"
frames="$(mktemp -d)"

cmake --build "$build" --target ssim_qt_panel_smoke_test -j2
SSIM_FRAMES_DIR="$frames" QT_QPA_PLATFORM=offscreen \
    "$build/tests/ssim_qt_panel_smoke_test" demoFrames
"$python" "$here/scripts/make_demo_gif.py" "$frames" "$here/docs/media/panel_demo.gif"
rm -rf "$frames"
