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
    # More of the text form, for --json's conversion (tools/ci.sh checks it).
    if grep -q MORE src/main.esk; then
        echo "src/main.esk:2:5: warning: format '%s' expects a string, but argument 2 has type 'int' [-Wformat]"
        echo "error: src/main.esk:3:9: undefined variable 'cuont' (did you mean 'count'?)"
        echo "note: src/main.esk:1:6: declared here"
        echo "  and a line of its own"
        echo "Parse failed!"
    fi
    exit 1
fi
echo "export default async () => ({});" > "$out"
