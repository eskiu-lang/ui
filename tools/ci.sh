#!/bin/sh
# CI on a released eskiuc: the runtime model, rejection and type-tag tests. Allocation
# counting, ASan and the self-host run in tools/check.py, which needs an eskiu checkout.
set -e
cd "$(dirname "$0")/.."
mkdir -p build
cc -c tests/heap.c -o build/heap.o
eskiuc tests/model/main.esk -O2 --link-arg build/heap.o -o build/model
build/model
eskiuc tests/tags.esk -o build/tags
build/tags
eskiuc tests/rejections.esk -o build/rejections
for case in "1:render loop" "2:hook kind/type changed" "3:duplicate key" "4:memo cycle" \
            "5:uncaught exception" "6:runtime left mid-phase" "7:duplicate key" "9:RefreshControl must come right before" "8:in Column\[0\] > TodoRow\[key 3\]"; do
    mode=${case%%:*}
    message=${case#*:}
    if build/rejections "$mode" > build/rejection.txt 2>&1; then
        echo "FAIL rejection $mode: exited 0"; exit 1
    fi
    grep -q "$message" build/rejection.txt || { cat build/rejection.txt; exit 1; }
    echo "PASS rejection: $message"
done
eskiuc tests/harness/headless.esk -o build/counter
build/counter
# The ui tool builds, and `ui new` makes a project on this checkout.
eskiuc tools/ui/main.esk -o build/ui
rm -rf build/newapp
(cd build && ./ui new newapp --ui "$PWD/..")
test -f build/newapp/src/main.esk && grep -q "ui	path" build/newapp/eskiu.lock
echo "PASS ui new"
# ui.toml is read before building: a malformed one stops with its line.
printf '[app]\nname = "New app"\n\n[ios]\nframeworks = ["CoreLocation", 3]\n' > build/newapp/ui.toml
if (cd build/newapp && ../ui build web) > build/uitoml.txt 2>&1; then echo "FAIL ui.toml: accepted"; exit 1; fi
grep -q "ui.toml:5: expected a string in the array" build/uitoml.txt || { cat build/uitoml.txt; exit 1; }
echo "PASS ui.toml errors"
# ui serve builds again on each change and answers the page's wait with the new build, or
# with a failed build's output. A stand-in compiler (tests/fake_eskiuc.sh) keeps it off emcc.
rm build/newapp/ui.toml
(cd build/newapp && ESKIUC="$PWD/../../tests/fake_eskiuc.sh" UI_PORT=8797 exec ../ui serve web > ../serve.txt 2>&1) &
echo $! > build/serve.pid
wait_for() {
    curl -s --max-time 30 "http://127.0.0.1:8797/__ui/wait?v=$1" > build/serve.json
    grep -q "$2" build/serve.json || { cat build/serve.json build/serve.txt; kill "$(cat build/serve.pid)"; exit 1; }
}
for i in 1 2 3 4 5 6 7 8 9 10; do curl -s "http://127.0.0.1:8797/" > build/serve.html && break; sleep 0.5; done
grep -q "/__ui/serve.mjs" build/serve.html || { cat build/serve.txt; exit 1; }
wait_for 0 '"version": 1, "error": ""'
echo "// BROKEN" >> build/newapp/src/main.esk
wait_for 1 '"version": 2, "error": "error: src/main.esk:1:1: broken'
sed -i.bak '/BROKEN/d' build/newapp/src/main.esk
wait_for 2 '"version": 3, "error": ""'
kill "$(cat build/serve.pid)"
echo "PASS ui serve"
# The icon and the version from ui.toml: the web build writes the icons, the manifest and
# the links to them; an icon that is not square stops the build and says why.
printf '[app]\nicon = "icon.png"\nversion = "2.0.0"\n' > build/newapp/ui.toml
cp tests/icon.png build/newapp/icon.png
(cd build/newapp && ESKIUC="$PWD/../../tests/fake_eskiuc.sh" ../ui build web) > build/icon.txt 2>&1 || { cat build/icon.txt; exit 1; }
for f in favicon.png apple-touch-icon.png icon-192.png icon-512.png manifest.webmanifest; do
    test -f "build/newapp/build/ui/web/$f" || { echo "FAIL icon: no $f"; exit 1; }
done
file build/newapp/build/ui/web/icon-192.png | grep -q "192 x 192" || { echo "FAIL icon: icon-192.png size"; exit 1; }
grep -q 'rel="manifest"' build/newapp/build/ui/web/index.html || { echo "FAIL icon: no manifest link"; exit 1; }
ls build/newapp/build/ui/web | grep -q "made\|stamp" && { echo "FAIL icon: a marker among the page's files"; exit 1; }
cp screenshots/tasks-web.png build/newapp/icon.png
if (cd build/newapp && ESKIUC="$PWD/../../tests/fake_eskiuc.sh" ../ui build web) > build/icon.txt 2>&1; then echo "FAIL icon: a wide icon was accepted"; exit 1; fi
grep -q "it must be square" build/icon.txt || { cat build/icon.txt; exit 1; }
echo "PASS ui icon"
