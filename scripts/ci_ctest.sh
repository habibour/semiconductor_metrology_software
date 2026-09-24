#!/usr/bin/env bash
# Runs ctest for CI and, when it fails, also reports the failing tests as a
# GitHub annotation. Annotations are readable through the public API without a
# login, while the raw job log is not, so a failure can be diagnosed remotely.
#
#   scripts/ci_ctest.sh BUILD_DIR [ctest args...]
set -u
build_dir="$1"
shift

log="$(mktemp)"
ctest --test-dir "$build_dir" --output-on-failure "$@" 2>&1 | tee "$log"
status=${PIPESTATUS[0]}

if [ "$status" -ne 0 ]; then
    # Failing test names first, then the output ctest printed for each failing
    # test (from its "Failed" line up to the next test starting).
    summary="$( { grep -E "^\s+[0-9]+ - |tests failed out of" "$log"
                  awk '/\*\*\*(Failed|Exception|Timeout)|Subprocess aborted/ {p=1; print; next} /^ +Start +[0-9]+:/ {p=0} p' "$log" | head -60
                } | head -80 | sed -e 's/%/%25/g' -e 's/\r//g' | awk '{printf "%s%%0A", $0}')"
    echo "::error title=ctest failed (exit ${status})::${summary}"
fi
exit "$status"
