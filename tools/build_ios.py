#!/usr/bin/env python3
"""Build a simulator app, using a pinned Yoga checkout; no Xcode project needed."""
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import shutil
import subprocess

HERE = Path(__file__).resolve().parents[1]
from paths import compiler as find_compiler
BUILD = HERE / "build/ios"
YOGA = Path(os.environ.get("YOGA_SOURCE", HERE / "build/yoga")).resolve()
REVISION = "042f5013152eb81c1552dec945b88f7b95ca350f"


def run(args):
    subprocess.run([str(arg) for arg in args], check=True)


def main():
    BUILD.mkdir(parents=True, exist_ok=True)
    if not YOGA.is_dir():
        raise SystemExit("Missing Yoga: git clone --depth 1 --branch v3.2.1 https://github.com/react/yoga.git " + str(YOGA))
    revision = subprocess.check_output(["git", "-C", YOGA, "rev-parse", "HEAD"], text=True).strip()
    if revision != REVISION:
        raise SystemExit("Unexpected Yoga revision: " + revision)
    sdk = subprocess.check_output(["xcrun", "--sdk", "iphonesimulator", "--show-sdk-path"], text=True).strip()
    compiler = subprocess.check_output(["xcrun", "--sdk", "iphonesimulator", "--find", "clang++"], text=True).strip()
    arch = os.environ.get("IOS_ARCH", os.uname().machine)
    target = arch + "-apple-ios15.0-simulator"
    common = [compiler, "-target", target, "-isysroot", sdk, "-I", YOGA, "-O2", "-fno-omit-frame-pointer"]
    sources = sorted((YOGA / "yoga").rglob("*.cpp"))
    objects = [BUILD / (str(source.relative_to(YOGA)).replace("/", "_") + ".o") for source in sources]

    def compile_one(pair):
        source, output = pair
        if not output.exists() or source.stat().st_mtime > output.stat().st_mtime:
            # Upstream Yoga requires C++20; our host uses C++17.
            run([*common, "-std=c++20", "-c", source, "-o", output])

    with ThreadPoolExecutor(max_workers=min(8, os.cpu_count() or 1)) as pool:
        list(pool.map(compile_one, zip(sources, objects)))
    host = BUILD / "host.o"
    run([*common, "-std=c++17", "-fobjc-arc", "-c", HERE / "ios/host.mm", "-o", host])
    esk = BUILD / "app.o"
    run([find_compiler(), HERE / "examples/app.esk", "--target", target,
         "-O2", "-o", esk])
    bundle = BUILD / "EskiuUI.app"
    bundle.mkdir(exist_ok=True)
    shutil.copyfile(HERE / "ios/Info.plist", bundle / "Info.plist")
    for asset in (HERE / "assets").iterdir():
        shutil.copyfile(asset, bundle / asset.name)
    run([*common, host, esk, *objects, "-framework", "UIKit", "-framework", "Foundation",
         "-framework", "CoreGraphics", "-framework", "QuartzCore", "-o", bundle / "EskiuUI"])
    run(["codesign", "--force", "--sign", "-", bundle])
    print("Built:", bundle)


if __name__ == "__main__":
    main()
