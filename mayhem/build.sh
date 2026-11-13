#!/usr/bin/env bash
#
# mayhem/build.sh — builds the UltraVNC fuzz harnesses (sanitized + standalone) and the behavioral
# self-test (normal flags). Runs inside the commit image (mayhem/Dockerfile) as `mayhem` in /mayhem;
# re-runnable offline: every dependency (zlib, zstd, liblzma) is an apt -dev package baked into the image.
#
# UltraVNC is a Windows codebase. The parts that build on Linux and parse untrusted bytes are compiled
# unmodified from upstream:
#   rdr/{Zlib,Zstd,xz}InStream.cxx + InStream.cxx   the viewer's decompressing input streams
#   rfb/{zrleDecode,xzDecode}.h (+ zywrle/xzyw templates)   ZRLE/ZYWRLE/XZ/XZYW rectangle decoders
#   repeater/webgui/*.c                              the repeater's embedded HTTP server
# The few MSVC/Win32 names they use come from mayhem/compat/ (force-included), and the repeater's
# Windows-only page handlers are stood in for by mayhem/webio_stubs.c. No upstream file is edited.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
OUT=/mayhem
B="$SRC/build"
rm -rf "$B"
mkdir -p "$B/fuzz" "$B/test"

CXX_COMMON="-std=c++17 -w -include $SRC/mayhem/compat/rdr_compat.h -D_XZ -D_VCPKG -I$SRC -I$SRC/mayhem"
C_COMMON="-w -DLINUX -include $SRC/mayhem/compat/webio_compat.h -I$SRC/repeater/webgui -I$SRC/mayhem"
RDR_SRCS="rdr/ZlibInStream.cxx rdr/ZstdInStream.cxx rdr/xzInStream.cxx rdr/InStream.cxx mayhem/uvnc_zrle.cpp mayhem/uvnc_xz.cpp"
WEB_SRCS="repeater/webgui/webio.c repeater/webgui/webutils.c repeater/webgui/webfs.c repeater/webgui/wsfdata.c repeater/webgui/webclib.c repeater/webgui/webobjs.c mayhem/webio_stubs.c mayhem/webio_drive.c"
LIBS="-l:libzstd.a -l:liblzma.a -l:libz.a"

# compile_project <dir> <flags...>: the fuzzed code (decoders, streams, web server) into objects.
compile_project() {
  local dir="$1"; shift
  local f
  for f in $RDR_SRCS; do
    $CXX "$@" $CXX_COMMON -c "$SRC/$f" -o "$dir/$(basename "${f%.*}").o"
  done
  # -fno-sanitize=function: webio.c calls form handlers as char*(*)(wi_sess*) while the real
  # repeater's CGI handlers (webtest.c, mirrored by webio_stubs.c) take (wi_sess*, EOFILE*). That
  # upstream mismatch is benign but halts every POST at webio.c:733; ASan and the rest of UBSan stay on.
  for f in $WEB_SRCS; do
    $CC "$@" -fno-sanitize=function $C_COMMON -c "$SRC/$f" -o "$dir/$(basename "${f%.*}").o"
  done
}

# 1) The project with sanitizers + DWARF-3 + SanCov (so the fuzzed code, not just the harness, is instrumented).
FUZZ_FLAGS="-O1 $SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link"
compile_project "$B/fuzz" $FUZZ_FLAGS
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$B/fuzz/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$B/fuzz/standalone_main.o"
PROJ_OBJS="$(ls "$B"/fuzz/*.o | grep -v -e standalone_main.o -e lsan_off.o | tr '\n' ' ')"

# 2) Each harness twice: libFuzzer, and the run-once standalone reproducer.
build_harness() {
  local name="$1" src="$2" compiler="$3" cflags="$4"
  $compiler $FUZZ_FLAGS $cflags -c "$SRC/mayhem/$src" -o "$B/fuzz/h_$name.o"
  $CXX $FUZZ_FLAGS $LIB_FUZZING_ENGINE "$B/fuzz/h_$name.o" $PROJ_OBJS "$B/fuzz/lsan_off.o" $LIBS -o "$OUT/$name"
  $CXX $FUZZ_FLAGS "$B/fuzz/h_$name.o" "$B/fuzz/standalone_main.o" $PROJ_OBJS "$B/fuzz/lsan_off.o" $LIBS -o "$OUT/$name-standalone"
}
build_harness fuzz_zrle  fuzz_zrle.cpp  "$CXX" "$CXX_COMMON"
build_harness fuzz_xz    fuzz_xz.cpp    "$CXX" "$CXX_COMMON"
build_harness fuzz_webio fuzz_webio.c   "$CC" "$C_COMMON"

# 3) The behavioral self-test with the project's NORMAL flags (clean build, no sanitizers).
compile_project "$B/test" -O1 $COVERAGE_FLAGS
$CXX -O1 $COVERAGE_FLAGS $CXX_COMMON -I$SRC -c "$SRC/mayhem/selftest.cpp" -o "$B/test/selftest.o"
$CXX -O1 $COVERAGE_FLAGS "$B/test/selftest.o" $(ls "$B"/test/*.o | grep -v selftest.o | tr '\n' ' ') $LIBS -o "$OUT/uvnc_selftest"
