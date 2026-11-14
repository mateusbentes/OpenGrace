#!/usr/bin/env sh
# ---------------------------------------------------------------------------
# tools/build-android-apk.sh
#
# The OpenGrace app shell packaging (decision 52): the engine's shared
# library built by the CMake target `grace_main` (libmain.so) goes into the
# SDL2 android-project template with the engine's overlay (the manifest,
# GraceActivity and the strings), and the APK is assembled with the build
# tools' own aapt2/d8/zipalign/apksigner.
#
# The flow is the one SDL2's android-project template expects:
#   1. the template's res (icons, theme) plus the engine's strings.xml
#      compile to the resources table (aapt2 compile/link);
#   2. the template's Java glue (SDLActivity and friends) plus the engine's
#      overlay (GraceActivity, R.java) compile to Java bytecode (javac);
#   3. the bytecode dexes (d8) and the dex plus the shared library zip into
#      the APK (zip);
#   4. the APK aligns (zipalign) and signs (apksigner).
#
# The engine is unchanged: the same sources build the Linux, Windows and
# macOS executables and this shared library — the shell is the mobile
# delivery path, not a different engine (decision 52).
#
# Requirements on the build machine:
#   - Android build-tools (aapt2, d8, zipalign, apksigner) via $ANDROID_SDK
#   - the NDK's javac and the platform android.jar via $ANDROID_SDK
#   - the engine's libmain.so built with GRACE_ENABLE_ANDROID_SHELL=ON
#     (set GRACE_MAIN_SO to the built file, or let the script find it)
# ---------------------------------------------------------------------------
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)

SDK=${ANDROID_SDK:-/tmp/android-sdk}
BT=${ANDROID_BUILD_TOOLS:-$SDK/build-tools/34.0.0}
PLATFORM=${ANDROID_PLATFORM_JAR:-$SDK/platforms/android-34/android.jar}
ABI=${ANDROID_ABI:-arm64-v8a}
OUT=${OUT:-$ROOT/build-android-apk}
# The test keystore lives outside $OUT because the script wipes $OUT on
# every run: a fresh keystore per run would sign each APK with a new key
# and the device would refuse the update (INSTALL_FAILED_UPDATE_INCOMPATIBLE).
KEYSTORE=${OG_KEYSTORE:-$ROOT/.og-android.keystore}

# The engine's shared library. The CMake target is `grace_main`, output
# `libmain.so`; the script takes the built file from the build tree.
MAIN_SO=${GRACE_MAIN_SO:-$ROOT/build-android-apk/libmain.so}
if [ ! -f "$MAIN_SO" ]; then
    echo "error: $MAIN_SO is not built; configure with" >&2
    echo "  -DGRACE_ENABLE_ANDROID_SHELL=ON -DGRACE_ENABLE_VULKAN=ON" >&2
    exit 1
fi

SDL2_TEMPLATE=$ROOT/sdl2/android-project/app/src/main
if [ ! -d "$SDL2_TEMPLATE/java/org/libsdl/app" ]; then
    echo "error: the SDL2 submodule is not initialised; run" >&2
    echo "  git submodule update --init sdl2" >&2
    exit 1
fi

rm -rf "$OUT"
mkdir -p "$OUT/res" "$OUT/gen" "$OUT/classes" "$OUT/lib/$ABI"

# The resources: the template's icons and theme, then the engine's strings.
cp -r "$SDL2_TEMPLATE/res"/* "$OUT/res/"
cp "$ROOT/android/app/src/main/res/values/strings.xml" "$OUT/res/values/"

# The manifest: the engine's overlay (the activity is GraceActivity).
"$BT/aapt2" compile --dir "$OUT/res" -o "$OUT/res.zip"
"$BT/aapt2" link -o "$OUT/base.apk" -I "$PLATFORM" \
    --manifest "$ROOT/android/app/src/main/AndroidManifest.xml" \
    "$OUT/res.zip" --java "$OUT/gen"

# The Java glue: the template's SDLActivity and the engine's overlay.
find "$SDL2_TEMPLATE/java" -name "*.java" > "$OUT/java.txt"
find "$OUT/gen" -name "*.java" >> "$OUT/java.txt"
find "$ROOT/android/app/src/main/java" -name "*.java" >> "$OUT/java.txt"
javac -source 8 -target 8 -classpath "$PLATFORM" \
    -d "$OUT/classes" @"$OUT/java.txt"

# The dex and the shared library go into the APK.
find "$OUT/classes" -name "*.class" > "$OUT/dex-input.txt"
sed 's|^'"$OUT"'/classes/||' "$OUT/dex-input.txt" > "$OUT/dex-input2.txt"
# The d8 in build-tools 34.0.0 fails with an NPE on the SDL2 template's
# anonymous inner classes; the 35.0.0 d8 handles them, so the script prefers
# it when the build machine has both.
D8=$BT/d8
if [ -x "$SDK/build-tools/35.0.0/d8" ]; then
    D8=$SDK/build-tools/35.0.0/d8
fi
(cd "$OUT/classes" && "$D8" --release --min-api 19 --lib "$PLATFORM" \
    --output "$OUT/classes" @"$OUT/dex-input2.txt")

cp "$MAIN_SO" "$OUT/lib/$ABI/libmain.so"
cp "$OUT/base.apk" "$OUT/og.apk"
# The dex goes at the APK root (the manifest expects classes.dex there).
(cd "$OUT/classes" && zip -q "$OUT/og.apk" classes.dex)
(cd "$OUT" && zip -q og.apk classes.dex lib/"$ABI"/libmain.so)

# The APK aligns and signs.
"$BT/zipalign" -f 4 "$OUT/og.apk" "$OUT/og-aligned.apk"
if [ ! -f "$KEYSTORE" ]; then
    keytool -genkeypair -keystore "$KEYSTORE" -alias og \
        -keyalg RSA -keysize 2048 -validity 10000 \
        -storepass opengrace -keypass opengrace -dname "CN=OpenGrace"
fi
"$BT/apksigner" sign --ks "$KEYSTORE" \
    --ks-pass pass:opengrace --key-pass pass:opengrace \
    --out "$OUT/og-signed.apk" \
    "$OUT/og-aligned.apk"

echo "APK: $OUT/og-signed.apk"
ls -la "$OUT/og-signed.apk"
