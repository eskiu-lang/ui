#!/bin/sh
# The host checks CI runs on a simulator, an emulator or a headless browser (docs/testing.md):
#
#   tools/ci_host.sh web       # emcc and Chrome (UI_CHROME), as on the Linux runner
#   tools/ci_host.sh macos     # the AppKit host in a window (an awake, unlocked display)
#   tools/ci_host.sh linux     # the GTK 4 host in a window (a display: Xvfb on CI)
#   tools/ci_host.sh ios       # a booted simulator, or the first iPhone, booted for it
#   tools/ci_host.sh android   # a device or a running emulator (adb)
#
# Every `ui agent` op (tests/agent/check.sh), the conformance scene's ui tests, then the
# speed budgets, which run the counter's and the benchmark's ui tests (tools/budgets.py).
set -e
HOST=${1:?usage: tools/ci_host.sh web|macos|linux|ios|android}
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
if [ "$HOST" = web ]; then
    python3 tools/budgets.py model
    # The prerendered first frame: content before the module, the elements adopted.
    python3 tests/web/prerender.py --measure 3
    # Asset updates: a bundle published, downloaded, used at the next load; bad ones refused.
    python3 tests/web/updates.py
fi
# macOS: a release of the conformance app is a universal, signed bundle.
if [ "$HOST" = macos ]; then
    started=$(date +%s)
    (cd tests/conformance && ../../build/ui release macos > ../../build/release-macos.txt 2>&1) || { cat build/release-macos.txt; exit 1; }
    archs=$(lipo -archs tests/conformance/build/ui/macos/conformance.app/Contents/MacOS/conformance)
    case "$archs" in *arm64*x86_64*|*x86_64*arm64*) ;; *) echo "FAIL release macos: $archs, not universal"; exit 1;; esac
    codesign --verify tests/conformance/build/ui/macos/conformance.app || { echo "FAIL release macos: not signed"; exit 1; }
    echo "release macos: $(( $(date +%s) - started )) s"
fi
# Linux: a release of the conformance app is the GTK host with the app in, no driver, packaged
# with its assets.
if [ "$HOST" = linux ]; then
    started=$(date +%s)
    (cd tests/conformance && ../../build/ui release linux > ../../build/release-linux.txt 2>&1) || { cat build/release-linux.txt; exit 1; }
    tarball=$(ls tests/conformance/build/ui/release/conformance-*-linux.tar.gz)
    tar -tzf "$tarball" | grep -q "^assets/logo.png$" || { echo "FAIL release linux: no assets in $tarball"; exit 1; }
    nm -D tests/conformance/build/ui/linux/conformance 2>/dev/null | grep -q ui_driver && { echo "FAIL release linux: the driver is in"; exit 1; }
    ldd tests/conformance/build/ui/linux/conformance | grep -q "libgtk-4" || { echo "FAIL release linux: not a GTK 4 program"; exit 1; }
    echo "release linux: $(( $(date +%s) - started )) s"
fi
