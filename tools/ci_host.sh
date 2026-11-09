#!/bin/sh
# The host checks CI runs on a simulator, an emulator or a headless browser (docs/testing.md):
#
#   tools/ci_host.sh web       # emcc and Chrome (UI_CHROME), as on the Linux runner
#   tools/ci_host.sh macos     # the AppKit host in a window (an awake, unlocked display)
#   tools/ci_host.sh ios       # a booted simulator, or the first iPhone, booted for it
#   tools/ci_host.sh android   # a device or a running emulator (adb)
#
# Every `ui agent` op (tests/agent/check.sh), the conformance scene's ui tests, then the
# speed budgets, which run the counter's and the benchmark's ui tests (tools/budgets.py).
set -e
HOST=${1:?usage: tools/ci_host.sh web|macos|ios|android}
cd "$(dirname "$0")/.."
mkdir -p build
eskiuc tools/ui/main.esk -o build/ui
started=$(date +%s)
tests/agent/check.sh "$HOST"
echo "agent check: $(( $(date +%s) - started )) s"
started=$(date +%s)
(cd tests/conformance && ../../build/ui test "$HOST")
echo "conformance: $(( $(date +%s) - started )) s"
started=$(date +%s)
python3 tools/budgets.py "$HOST"
echo "budgets: $(( $(date +%s) - started )) s"
if [ "$HOST" = web ]; then python3 tools/budgets.py model; fi
# macOS: a release of the conformance app is a universal, signed bundle.
if [ "$HOST" = macos ]; then
    started=$(date +%s)
    (cd tests/conformance && ../../build/ui release macos > ../../build/release-macos.txt 2>&1) || { cat build/release-macos.txt; exit 1; }
    archs=$(lipo -archs tests/conformance/build/ui/macos/conformance.app/Contents/MacOS/conformance)
    case "$archs" in *arm64*x86_64*|*x86_64*arm64*) ;; *) echo "FAIL release macos: $archs, not universal"; exit 1;; esac
    codesign --verify tests/conformance/build/ui/macos/conformance.app || { echo "FAIL release macos: not signed"; exit 1; }
    echo "release macos: $(( $(date +%s) - started )) s"
fi
