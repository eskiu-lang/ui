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
