#!/bin/sh
# The host checks CI runs on a simulator, an emulator or a headless browser (docs/testing.md):
#
#   tools/ci_host.sh web       # emcc and Chrome (UI_CHROME), as on the Linux runner
#   tools/ci_host.sh ios       # a booted simulator, or the first iPhone, booted for it
#   tools/ci_host.sh android   # a device or a running emulator (adb)
#
# Every `ui agent` op (tests/agent/check.sh), the conformance scene's ui tests, then the
# speed budgets, which run the counter's and the benchmark's ui tests (tools/budgets.py).
set -e
HOST=${1:?usage: tools/ci_host.sh web|ios|android}
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
