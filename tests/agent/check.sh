#!/bin/sh
# Every `ui agent` op on a host, against tests/agent (an app with a test_id on each part):
#
#   tests/agent/check.sh web       # needs emcc and Chrome (UI_CHROME to pick one)
#   tests/agent/check.sh ios       # a booted simulator, or the first iPhone it boots
#   tests/agent/check.sh android   # a device or a running emulator (adb)
#
# It builds the ui tool, lets `ui agent` start `ui serve HOST` (and the browser), drives
# the app, checks each answer and stops what it started.
set -e
HOST=${1:-web}
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
UI="$ROOT/build/ui"
OUT="$ROOT/build/agent-$HOST"
mkdir -p "$OUT"
eskiuc "$ROOT/tools/ui/main.esk" -o "$UI"
export UI_PORT=${UI_PORT:-8796}
cd "$HERE"
fail() { echo "FAIL $HOST: $1"; "$UI" agent "$HOST" --stop > /dev/null 2>&1 || true; exit 1; }
# One op, its answer in $OUT/last.json; it must hold each pattern after the op.
op() {
    request=$1; shift
    "$UI" agent "$HOST" --json "$request" > "$OUT/last.json" 2> "$OUT/last.err" || {
        cat "$OUT/last.json" "$OUT/last.err"; fail "$request"; }
    for pattern in "$@"; do
        grep -q -- "$pattern" "$OUT/last.json" || { cat "$OUT/last.json"; fail "$request: no $pattern"; }
    done
    echo "PASS $HOST: $request"
}
# An op the driver must refuse, with this pattern in its answer.
refused() {
    request=$1; shift
    if "$UI" agent "$HOST" --json "$request" > "$OUT/last.json" 2> "$OUT/last.err"; then fail "$request: accepted"; fi
    for pattern in "$@"; do
        grep -q -- "$pattern" "$OUT/last.json" || { cat "$OUT/last.json"; fail "$request: no $pattern"; }
    done
    echo "PASS $HOST: $request (refused)"
}
# Starts ui serve and the app afresh, and takes the clock.
"$UI" agent "$HOST" --stop > /dev/null 2>&1 || true
"$UI" agent "$HOST" state > "$OUT/start.txt" 2>&1 || { cat "$OUT/start.txt"; fail "attach"; }
# A fresh start: the app as it launches.
op tree '"test_id":"add"' '"text":"Count: 0"' '"kind":"TextInput","test_id":"title"'
op "tap add" '"ok":true,"op":"tap","target":"add"'
op "tap Add" '"kind":"Button"'
op state '"signals":\[2,'
op "type title Milk bread" '"op":"type"'
op tree '"text":"Title: Milk bread"'
refused "tap echo" '"ok":false' 'takes no press'
refused "tap nothing" 'no element with test_id or accessible name'
# The virtual clock: a one second timeout fires only when the clock gets there.
op "tap later" '"ok":true'
op "clock +600" '"busy":"a timeout"'
op tree '"text":"Waiting"'
sleep 1
op tree '"text":"Waiting"'
op "clock +400" '"now":'
op tree '"text":"Done"'
# An animated value (500 ms) and a transition (300 ms): halfway after 250 ms, held there
# between ops (two screenshots a second apart are the same), at the end after settle.
op "tap move" '"ok":true'
op "clock +250" '"busy":"use_frame a transition"'
op tree '"text":"Moved: 4[0-9]"'
op "screenshot $OUT/one.png" '"width":[1-9]'
sleep 1
op "screenshot $OUT/two.png" '"width":[1-9]'
cmp -s "$OUT/one.png" "$OUT/two.png" || fail "the screen moved between two ops"
echo "PASS $HOST: nothing moves between ops"
op "clock settle" '"idle":true'
op tree '"text":"Moved: 100"'
op "screenshot $OUT/three.png" '"width":[1-9]'
cmp -s "$OUT/one.png" "$OUT/three.png" && fail "settle did not finish the animations"
echo "PASS $HOST: settle finished the animations"
op layout '"test_id":"add"' '"width":[1-9]' '"window":{"width":[1-9]'
op "scroll list 200" '"op":"scroll"' '"offset":[1-9]'
op "tap open" '"ok":true'
op state '"modals":1'
op back '"handled":true'
op state '"modals":0'
op back '"handled":false'
op logs '"core":\['
op perf '"frames":[1-9]' '"memory_bytes":[1-9]'
op "clock real" '"clock":"real"'
"$UI" agent "$HOST" --stop > /dev/null 2>&1 || true
echo "PASS $HOST: every op"
