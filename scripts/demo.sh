#!/usr/bin/env bash
# One-command local demo (PRD §12.2): builds if needed, starts the machine
# with the SECS/GEM link on an OS-chosen port, runs scenarios/normal_run.scn
# against it with host_sim, prints the message trace, leaves the wafer's
# results on disk, and shuts the machine down. macOS only (PRD D-11); no
# Docker, no Linux or Windows artifacts.
#
#   scripts/demo.sh                # uses ./build, configuring it if absent
#   scripts/demo.sh /path/to/build # uses an existing build directory
#
# Exit code follows the scenario result: 0 pass, 1 fail, 2 setup problem.

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${1:-${ROOT_DIR}/build}"
OUT_DIR="$(mktemp -d "${TMPDIR:-/tmp}/ssim_demo.XXXXXX")"

MACHINE_PID=""
cleanup() {
    if [ -n "${MACHINE_PID}" ] && kill -0 "${MACHINE_PID}" 2>/dev/null; then
        kill -TERM "${MACHINE_PID}" 2>/dev/null
        wait "${MACHINE_PID}" 2>/dev/null
    fi
}
trap cleanup EXIT

if [ ! -d "${BUILD_DIR}" ]; then
    echo "== configuring ${BUILD_DIR} (Release) =="
    cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release || exit 2
fi
echo "== building equipment_cli and host_sim =="
cmake --build "${BUILD_DIR}" -j --target equipment_cli host_sim || exit 2

CLI="${BUILD_DIR}/src/app_cli/equipment_cli"
HOST="${BUILD_DIR}/src/host_sim/host_sim"
if [ ! -x "${CLI}" ] || [ ! -x "${HOST}" ]; then
    echo "demo: expected binaries not found (${CLI}, ${HOST})" >&2
    exit 2
fi

echo "== starting the machine (port chosen by the OS, results in ${OUT_DIR}) =="
"${CLI}" serve --port 0 --control remote --rtf 0 --out "${OUT_DIR}" > "${OUT_DIR}/machine_stdout.log" &
MACHINE_PID=$!

PORT=""
DEADLINE=$((SECONDS + 30))
while [ ${SECONDS} -lt ${DEADLINE} ]; do
    if ! kill -0 "${MACHINE_PID}" 2>/dev/null; then
        echo "demo: the machine exited before it started listening; see ${OUT_DIR}/machine_stdout.log" >&2
        cat "${OUT_DIR}/machine_stdout.log" >&2
        exit 2
    fi
    PORT="$(grep -o 'listening on [0-9.]*:[0-9]*' "${OUT_DIR}/machine_stdout.log" 2>/dev/null | head -1 | grep -o '[0-9]*$')"
    if [ -n "${PORT}" ]; then
        break
    fi
    sleep 0.2
done
if [ -z "${PORT}" ]; then
    echo "demo: the machine never reported a listening port" >&2
    exit 2
fi
echo "machine listening on 127.0.0.1:${PORT}"

echo "== running scenarios/normal_run.scn with host_sim =="
"${HOST}" --script "${ROOT_DIR}/scenarios/normal_run.scn" --connect "127.0.0.1:${PORT}"
RESULT=$?

echo "== shutting the machine down =="
kill -TERM "${MACHINE_PID}" 2>/dev/null
wait "${MACHINE_PID}" 2>/dev/null
MACHINE_PID=""

echo "== results left in ${OUT_DIR} =="
find "${OUT_DIR}" -maxdepth 3 -type f | sort

exit ${RESULT}
