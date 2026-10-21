#!/bin/sh
# A stand-in for eskiuc in tools/ci.sh's ui serve check: it writes the -o file, and fails
# when the app's main says BROKEN.
out=""
while [ $# -gt 0 ]; do
    if [ "$1" = "-o" ]; then out=$2; fi
    shift
done
if grep -q BROKEN src/main.esk; then
    echo "error: src/main.esk:1:1: broken"
    exit 1
fi
echo "export default async () => ({});" > "$out"
