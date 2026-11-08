#!/bin/bash
# Apollo DNA engine — headless test harness build.
#
# Compiles the JUCE modules Apollo's Analysis engine needs, the Analysis
# sources themselves, and the test harness, then links a console test
# runner. No JUCE project files, no IDE, no plugin formats — one script.
#
# Usage: ./build.sh        (recompiles Apollo sources + harness; JUCE .o cached)
#        ./build.sh clean  (wipes build/ incl. cached JUCE objects)
set -euo pipefail
cd "$(dirname "$0")"

if [ "${1:-}" = "clean" ]; then rm -rf build; fi

PLUGIN="${APOLLO_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
JUCE_MOD=$PLUGIN/build/_deps/juce-src/modules
SRC=$PLUGIN/Source
OUT=$PWD/build
mkdir -p "$OUT"

CXX="clang++"
STD="-std=c++17 -O1"
# Same feature flags as the plugin target (apollo-plugin/CMakeLists.txt):
# no web browser, no curl (keeps the link light), no splash screen.
# DONT_SET_USING_JUCE_NAMESPACE is intentionally NOT defined: like the
# plugin's generated JuceHeader.h, the test JuceHeader.h does
# `using namespace juce` (the Analysis sources rely on it).
DEFS="-DJUCE_WEB_BROWSER=0 -DJUCE_USE_CURL=0 -DJUCE_VST3_CAN_REPLACE_VST2=0 -DJUCE_DISPLAY_SPLASH_SCREEN=0 -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DNDEBUG"
INCS="-I$JUCE_MOD -I$PWD -I$SRC"
# macOS frameworks needed by juce_core / juce_audio_formats / juce_dsp:
FW="-framework Cocoa -framework IOKit -framework Accelerate -framework AudioToolbox -framework CoreAudio -framework QuartzCore -framework Security"

echo "== JUCE modules (one TU each — JUCE requires module cpps standalone) =="
echo "   (on Apple JUCE compiles the .mm wrapper = ObjC++ entry point)"
for m in juce_core juce_events juce_audio_basics juce_audio_formats juce_dsp; do
    if [ ! -f "$OUT/$m.o" ]; then
        echo "   $m"
        $CXX $STD $DEFS $INCS -c "$JUCE_MOD/$m/$m.mm" -o "$OUT/$m.o"
    else
        echo "   $m (cached)"
    fi
done

# juce_audio_formats bundles libFLAC as two amalgamated C files (same as the
# plugin's CMake build compiles). These are C, not C++ — hence `cc`.
for f in 1 2; do
    if [ ! -f "$OUT/juce_flac_$f.o" ]; then
        echo "   flac_$f"
        cc -O1 $DEFS -I"$JUCE_MOD" -c \
            "$JUCE_MOD/juce_audio_formats/juce_audio_formats_flac_$f.c" \
            -o "$OUT/juce_flac_$f.o"
    else
        echo "   flac_$f (cached)"
    fi
done

# juce_core's bundled extras (same files the plugin's CMake build compiles):
# the compilation-timestamp globals and JUCE's zlib (for ZipFile::Builder).
echo "   juce_core extras"
$CXX $STD $DEFS $INCS -c "$JUCE_MOD/juce_core/juce_core_CompilationTime.cpp" \
    -o "$OUT/juce_CompilationTime.o"
cc -O1 $DEFS -I"$JUCE_MOD" -c "$JUCE_MOD/juce_core/juce_core_zlib.c" \
    -o "$OUT/juce_zlib.o"

echo "== Apollo Analysis sources =="
for f in DnaExtractor DnaLibrary MatchAnalyzer; do
    echo "   $f"
    $CXX $STD $DEFS $INCS -c "$SRC/Analysis/$f.cpp" -o "$OUT/$f.o"
done

echo "== test harnesses =="
$CXX $STD $DEFS $INCS -c test_dna.cpp -o "$OUT/test_dna.o"
$CXX "$OUT"/juce_*.o "$OUT"/Dna*.o "$OUT"/MatchAnalyzer.o "$OUT"/test_dna.o -o "$OUT/test_dna" $FW
echo "built: $OUT/test_dna"

if [ -f test_features.cpp ]; then
    $CXX $STD $DEFS $INCS -c test_features.cpp -o "$OUT/test_features.o"
    $CXX "$OUT"/juce_*.o "$OUT"/Dna*.o "$OUT"/MatchAnalyzer.o "$OUT"/test_features.o -o "$OUT/test_features" $FW
    echo "built: $OUT/test_features"
fi
