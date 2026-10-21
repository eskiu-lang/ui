#!/usr/bin/env python3
"""Build a native Android APK using installed SDK/NDK tools; no Gradle downloads."""
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import subprocess
import zipfile

HERE = Path(__file__).resolve().parents[1]
from paths import compiler as find_compiler
SDK = Path(os.environ.get("ANDROID_HOME", Path.home() / "Library/Android/sdk"))
NDK = Path(os.environ.get("ANDROID_NDK_HOME", SDK / "ndk/28.2.13676358"))
YOGA = Path(os.environ.get("YOGA_SOURCE", HERE / "build/yoga"))
BUILD = HERE / "build/android"


def run(args):
    subprocess.run([str(arg) for arg in args], check=True)


def main():
    BUILD.mkdir(parents=True, exist_ok=True)
    revision = subprocess.check_output(["git", "-C", YOGA, "rev-parse", "HEAD"], text=True).strip()
    if revision != "042f5013152eb81c1552dec945b88f7b95ca350f":
        raise SystemExit("unexpected Yoga revision")
    tools = SDK / "build-tools/36.0.0"
    platform = SDK / "platforms/android-35/android.jar"
    clang = NDK / "toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android24-clang++"
    common = [clang, "-fPIC", "-O2", "-I", YOGA]
    sources = sorted((YOGA / "yoga").rglob("*.cpp"))
    objects = [BUILD / (str(source.relative_to(YOGA)).replace("/", "_") + ".o") for source in sources]

    def compile_one(pair):
        source, output = pair
        if not output.exists() or source.stat().st_mtime > output.stat().st_mtime:
            run([*common, "-std=c++20", "-c", source, "-o", output])

    with ThreadPoolExecutor(max_workers=min(8, os.cpu_count() or 1)) as pool:
        list(pool.map(compile_one, zip(sources, objects)))
    host = BUILD / "host.o"
    run([*common, "-std=c++17", "-c", HERE / "hosts/android/host.cpp", "-o", host])
    # The native example's code, which scene 6 of the test app uses.
    native = HERE / "examples/native/native/android"
    natives = []
    for source in sorted(native.glob("*.c")):
        output = BUILD / ("native-" + source.stem + ".o")
        run([*common, "-x", "c", "-I", HERE / "hosts/android", "-c", source, "-o", output])
        natives.append(output)
    esk = BUILD / "app.o"
    run([find_compiler(), HERE / "tests/harness/app.esk", "--target",
         "aarch64-linux-android24", "-O2", "-o", esk])
    library = BUILD / "libeskiu_ui.so"
    run([clang, "-shared", "-static-libstdc++", "-Wl,-z,max-page-size=16384", host, esk, *natives, *objects, "-llog", "-o", library])
    classes = BUILD / "classes"
    dex = BUILD / "dex"
    classes.mkdir(exist_ok=True); dex.mkdir(exist_ok=True)
    bootclasspath = str(tools / "core-lambda-stubs.jar") + os.pathsep + str(platform)
    run(["javac", "-source", "8", "-target", "8", "-bootclasspath", bootclasspath, "-d", classes, HERE / "hosts/android/UiActivity.java",
         *sorted(native.glob("*.java"))])
    run([tools / "d8", "--min-api", "24", "--lib", platform, "--output", dex, *sorted(classes.rglob("*.class"))])
    unsigned = BUILD / "unsigned.apk"
    run([tools / "aapt", "package", "-f", "-M", HERE / "hosts/android/AndroidManifest.xml", "-A", HERE / "tests/harness/assets",
         "-I", platform, "-F", unsigned])
    with zipfile.ZipFile(unsigned, "a") as apk:
        apk.write(dex / "classes.dex", "classes.dex")
        apk.write(library, "lib/arm64-v8a/libeskiu_ui.so")
    aligned = BUILD / "aligned.apk"
    run([tools / "zipalign", "-f", "-p", "4", unsigned, aligned])
    keystore = BUILD / "debug.keystore"
    if not keystore.exists():
        run(["keytool", "-genkeypair", "-keystore", keystore, "-storepass", "android", "-keypass", "android",
             "-alias", "phase0", "-keyalg", "RSA", "-validity", "3650", "-dname", "CN=Eskiu Phase 0", "-noprompt"])
    output = BUILD / "EskiuUI.apk"
    run([tools / "apksigner", "sign", "--ks", keystore, "--ks-pass", "pass:android", "--out", output, aligned])
    run([tools / "apksigner", "verify", output])
    print("Built:", output)


if __name__ == "__main__":
    main()
