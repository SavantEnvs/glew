#!/usr/bin/env bash
#
# mayhem/build.sh — build this repo's fuzz harness(es). EDIT per repo.
#
# Runs inside the commit image (mayhem/Dockerfile) as `mayhem` in /mayhem. The base image
# (ghcr.io/savantenvs/base) already exports the build contract — use these, don't redefine:
#   CC, CXX             stock clang / clang++
#   LIB_FUZZING_ENGINE  -fsanitize=fuzzer   (link into each harness that has a LLVMFuzzer entry)
#   SANITIZER_FLAGS     -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
#                       (ASan + UBSan, both set to HALT — so Mayhem catches memory AND UB defects)
#   DEBUG_FLAGS         -g -gdwarf-3   (DWARF debug info — always on for fuzz/standalone builds,
#                       independent of the sanitizer off-switch; DWARF version must be < 4)
#   RUST_DEBUG_FLAGS    -C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3
#                       (thread through RUSTFLAGS on every cargo-fuzz build)
#   GO_DEBUG_FLAGS      -gcflags=all=-N -l
#                       (thread through go build / go-fuzz-build so the linked ELF keeps symbols)
#   SRC                 /mayhem (the repo source)
#
# Contract: build EVERYTHING here — one runnable binary per fuzz harness, AND the project's test
# suite (so mayhem/test.sh only has to RUN it, never compile). Keep it ADDITIVE (build upstream as
# upstream documents; don't edit upstream files). IMPORTANT: build the PROJECT ITSELF with
# $SANITIZER_FLAGS and $DEBUG_FLAGS (not just the harness) so the fuzzed code is instrumented
# AND carries DWARF < 4 symbols — otherwise ASan/UBSan only see the harness, not the library
# you're trying to find bugs in, and backtraces won't resolve project source lines. Build the TEST
# suite with the project's NORMAL flags (a clean, independent build) so test.sh stays an honest
# functional oracle and won't false-fail on benign UB. Leave the test binary/runner where test.sh
# expects it.
set -euo pipefail

# clang rejects SOURCE_DATE_EPOCH='' (empty) — it must be unset or a valid integer.
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

# Build knobs come from the ENVIRONMENT (overridable), with sane defaults — no if-statements,
# just parameter-expansion fallbacks. Default sanitizers are the base's ASan+UBSan (halting); override
# per build via the Dockerfile's `--build-arg SANITIZER_FLAGS="..."`.
# NB: SANITIZER_FLAGS uses `=` (no colon) on purpose — `=` only fills when the var is UNSET, so an
# explicit EMPTY value (`--build-arg SANITIZER_FLAGS=`) is honored and builds with NO sanitizers
# (useful when you want the program's natural crash / full backtrace, not an ASan report). The other
# knobs use `:=` (default on empty too). MAYHEM_JOBS sets build parallelism (falls back to nproc).
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
# DEBUG_FLAGS carries DWARF debug info INDEPENDENTLY of the sanitizer off-switch (so an empty
# SANITIZER_FLAGS still yields DWARF symbols). DWARF MUST be < 4 (Mayhem triage can't read >=4); clang-19's
# plain `-g` emits DWARF-5, so `-gdwarf-3` is explicit. Apply $DEBUG_FLAGS to the fuzz/harness/standalone
# builds (NOT the test/oracle build). Rust/Go carry the same intent via their language flags below.
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${RUST_DEBUG_FLAGS:=-C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3}"
: "${GO_DEBUG_FLAGS:=-gcflags=all=-N -l}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
# COVERAGE_FLAGS: empty by default → no effect on the normal oracle build. Set it via the Dockerfile's
# `--build-arg COVERAGE_FLAGS="-fprofile-instr-generate -fcoverage-mapping"` to instrument the TEST
# build for source-coverage measurement (how much of the project the test suite actually exercises —
# a quality signal for the oracle; complements the anti-reward-hack sabotage check). APPEND it to the
# test build's compile+link flags in step 3 (NOT the fuzz build); after `test.sh` runs, merge with
# `llvm-profdata` and report with `llvm-cov`. Empty value is honored (`=`, not `:=`).
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS RUST_DEBUG_FLAGS GO_DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

# 1) Generate glew's sources. src/glew.c and include/GL/*.h are not in git: upstream generates them
#    with auto/Makefile from the Khronos OpenGL/EGL registries + nigels-com/glfixes, which its Makefile
#    would `git clone`. The Dockerfile fetched those, pinned, under /opt/toolchains/deps; copy them to
#    where auto/Makefile expects its clones (all gitignored) and mark them present so it never clones.
#    Re-runs are no-ops (make sees the outputs up to date).
: "${GLEW_OPENGL_REGISTRY:?set by mayhem/Dockerfile}" "${GLEW_EGL_REGISTRY:?}" "${GLEW_GLFIXES:?}"
stage_dep() {  # <pinned source> <auto/ dir name>
  if [ ! -f "auto/$2/.dummy" ]; then
    rm -rf "auto/$2"
    cp -r "$1" "auto/$2"
    chmod -R u+w "auto/$2"
    touch "auto/$2/.dummy"
  fi
}
stage_dep "$GLEW_OPENGL_REGISTRY" OpenGL-Registry
stage_dep "$GLEW_EGL_REGISTRY" EGL-Registry
stage_dep "$GLEW_GLFIXES" glfixes
make -C auto PYTHON=python3 ../include/GL/glew.h ../include/GL/glxew.h ../include/GL/eglew.h \
  ../include/GL/wglew.h ../src/glew.c ../src/glewinfo.c

OUT="$SRC/out"        # gitignored by upstream (/out)
mkdir -p "$OUT"
GLEW_DEFS="-DGLEW_STATIC -DGLEW_NO_GLU"
OSMESA_DEFS="$GLEW_DEFS -DGLEW_OSMESA -I$SRC/mayhem/stub"
GLX_DEFS="$GLEW_DEFS"
EGL_DEFS="$GLEW_DEFS -DGLEW_EGL"
INC="-I$SRC/include -I$SRC/mayhem/harnesses"

# Flag-reset TU for the harnesses/selftest: clears every GLEW_* / GLXEW_* / EGLEW_* support flag so
# each fuzz input starts from the same state (glewContextInit only resets its own string table).
gen_reset() {
  local out="$1"
  {
    echo "/* generated by mayhem/build.sh from src/glew.c */"
    for p in GLEW GLXEW EGLEW; do
      case $p in GLXEW) echo "#if MOCK_WITH_GLX";; EGLEW) echo "#if defined(GLEW_EGL)";; esac
      grep -oE "^GLboolean __${p}_[A-Za-z0-9_]+ = GL_FALSE;" src/glew.c \
        | sed -E 's/^GLboolean (__[A-Za-z0-9_]+) = GL_FALSE;/extern unsigned char \1;/'
      [ $p = GLEW ] || echo "#endif"
    done
    echo "void glew_mock_reset_flags(void) {"
    for p in GLEW GLXEW EGLEW; do
      case $p in GLXEW) echo "#if MOCK_WITH_GLX";; EGLEW) echo "#if defined(GLEW_EGL)";; esac
      grep -oE "^GLboolean __${p}_[A-Za-z0-9_]+ = GL_FALSE;" src/glew.c \
        | sed -E 's/^GLboolean (__[A-Za-z0-9_]+) = GL_FALSE;/  \1 = 0;/'
      [ $p = GLEW ] || echo "#endif"
    done
    echo "}"
  } > "$out.tmp"
  cmp -s "$out.tmp" "$out" && rm -f "$out.tmp" || mv "$out.tmp" "$out"
}
gen_reset "$OUT/glew_mock_reset.c"

FUZZ_CFLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS -O1"
# libFuzzer coverage for the project + harness objects of the fuzz binaries; the standalone
# reproducers get the same code without it (they link no libFuzzer runtime).
COV_FLAGS="-fsanitize=fuzzer-no-link"

# 2) The project (src/glew.c) once per backend (x fuzz/standalone), instrumented, plus the reset TU
#    and the LSan hook.
for v in fuzz sa; do
  vf=""; [ $v = fuzz ] && vf="$COV_FLAGS"
  $CC $FUZZ_CFLAGS $vf $OSMESA_DEFS $INC -c src/glew.c -o "$OUT/glew_osmesa.$v.o"
  $CC $FUZZ_CFLAGS $vf $GLX_DEFS    $INC -c src/glew.c -o "$OUT/glew_glx.$v.o"
  $CC $FUZZ_CFLAGS $vf $EGL_DEFS    $INC -c src/glew.c -o "$OUT/glew_egl.$v.o"
done
$CC $FUZZ_CFLAGS -c "$OUT/glew_mock_reset.c" -o "$OUT/reset_osmesa.o"
$CC $FUZZ_CFLAGS -DMOCK_WITH_GLX=1 -c "$OUT/glew_mock_reset.c" -o "$OUT/reset_glx.o"
$CC $FUZZ_CFLAGS -DGLEW_EGL -c "$OUT/glew_mock_reset.c" -o "$OUT/reset_egl.o"
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o "$OUT/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$OUT/standalone_main.o"

# 3) Each harness twice: libFuzzer binary + run-once standalone reproducer.
build_harness() {  # <name> <backend> <defs>
  local name="$1" be="$2" defs="$3"
  $CC $FUZZ_CFLAGS $COV_FLAGS $defs $INC -c "mayhem/harnesses/$name.c" -o "$OUT/$name.fuzz.o"
  $CC $FUZZ_CFLAGS $defs $INC -c "mayhem/harnesses/$name.c" -o "$OUT/$name.sa.o"
  $CXX $FUZZ_CFLAGS $LIB_FUZZING_ENGINE "$OUT/$name.fuzz.o" "$OUT/glew_$be.fuzz.o" "$OUT/reset_$be.o" \
    "$OUT/lsan_off.o" -o "$OUT/$name"
  $CXX $FUZZ_CFLAGS "$OUT/standalone_main.o" "$OUT/$name.sa.o" "$OUT/glew_$be.sa.o" "$OUT/reset_$be.o" \
    "$OUT/lsan_off.o" -o "$OUT/$name-standalone"
}
build_harness fuzz_glew_init  osmesa "$OSMESA_DEFS"
build_harness fuzz_glew_query osmesa "$OSMESA_DEFS"
build_harness fuzz_glxew      glx    "$GLX_DEFS"
build_harness fuzz_eglew      egl    "$EGL_DEFS"

# 4) Known-answer selftest (mayhem/test.sh runs it), with normal flags — no sanitizers.
$CC -O1 $COVERAGE_FLAGS $OSMESA_DEFS $INC mayhem/tests/glew_selftest.c src/glew.c \
  "$OUT/glew_mock_reset.c" -o "$OUT/glew_selftest"

ls -l "$OUT"/fuzz_*[a-z] "$OUT/glew_selftest"
