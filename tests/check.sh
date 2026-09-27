#!/bin/sh
# Builds the operations and their checks (tests/check-*.c) twice, as usual
# and with AddressSanitizer and UndefinedBehaviorSanitizer, and runs the
# checks with both. With the Flatpak GIMP everything builds and runs
# inside it (gimp-devtools/gimp-build.sh, found next to this
# repository or at $GIMP_BUILD); with GIMP_FLATPAK=0 it uses the system
# GEGL. Needs no network or display; the test images are generated.
#
#   tests/check.sh                 both builds, every operation
#   tests/check.sh quick           only the usual build
#   tests/check.sh [quick] noise   only forensics:noise (or another; "infinite"
#                                  is every operation on an infinite input)
#
# The builds and checks run isolated from your folders (GIMP_RUN_HOME,
# tests/output/gimp-home: see gimp-build.sh), so that nothing lands in
# ~/.var/app/org.gimp.GIMP. Before and after, it lists your folders of
# GIMP and the other apps (gimp-devtools/snapshot.sh, skipped
# without it) and fails if anything there changed.
#
# The build folders are in tests/output. Exits with 1 if anything failed.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
set -e

here=$(cd "$(dirname "$0")" && pwd)
top=$(dirname "$here")
out="$here/output"
mkdir -p "$out"
src=$top
GIMP_RUN_HOME=${GIMP_RUN_HOME:-$here/output/gimp-home}
export GIMP_RUN_HOME
# shellcheck source=SCRIPTDIR/isolate.sh
. "$here/isolate.sh"
snapshot_take "$out/snapshot-check-before.txt"

quick=
[ "$1" = quick ] && { quick=1; shift; }
ops=${*:-error-level jpeg-ghost noise luminance-gradient clone-detect pca infinite}

gimp_build=${GIMP_BUILD:-$top/../gimp-devtools/gimp-build.sh}
if [ "${GIMP_FLATPAK:-1}" = 1 ] && command -v flatpak >/dev/null 2>&1 &&
   flatpak info org.gimp.GIMP >/dev/null 2>&1; then
  [ -x "$gimp_build" ] || { echo "no gimp-build.sh at $gimp_build (set GIMP_BUILD)"; exit 1; }
  in_sdk () { "$gimp_build" "$top" "$*"; }
else
  in_sdk () { (cd "$top" && sh -c "$*"); }
fi

# builds into tests/output/<name> with the given meson options
build () {
  name=$1; shift
  if [ -f "$out/$name/build.ninja" ]; then
    in_sdk ninja -C "tests/output/$name" >/dev/null
  else
    in_sdk meson setup "tests/output/$name" "$@" >/dev/null
    in_sdk ninja -C "tests/output/$name" >/dev/null
  fi
}

status=0

# run_checks <build> <log suffix> <environment>
run_checks () {
  b=$1 suffix=$2 env=$3
  for op in $ops; do
    [ -x "$out/$b/tests/check-$op" ] || continue
    rm -rf "$out/tmp-$b"
    mkdir -p "$out/tmp-$b"
    # (check-infinite takes every module)
    mods="tests/output/$b/$op.so"
    [ "$op" = infinite ] && mods=$(cd "$top" && echo tests/output/"$b"/*.so)
    in_sdk "TMPDIR=tests/output/tmp-$b $env tests/output/$b/tests/check-$op \
      $mods" >"$out/check-$op$suffix.log" 2>&1 || status=1
    rm -rf "$out/tmp-$b"
    echo "-- $op"
    grep -E '^(FAIL|SKIP)|passed,' "$out/check-$op$suffix.log" || true
    grep -qE '^[0-9]+ passed, 0 failed' "$out/check-$op$suffix.log" || {
      echo "FAIL  $op: the checks failed or did not finish (tests/output/check-$op$suffix.log)"
      status=1; }
  done
}

echo "== usual build"
build build-check -Dwarning_level=2
run_checks build-check "" ""

if [ -n "$quick" ]; then
  snapshot_check "$out/snapshot-check-before.txt" "" || status=1
  exit $status
fi

echo
echo "== AddressSanitizer and UndefinedBehaviorSanitizer build"
build build-asan -Db_sanitize=address,undefined -Db_lundef=false
# GEGL, babl and the libraries under them keep caches until the end on
# purpose (types, conversions): a leak counts only when our code made the
# allocation itself, that is, when the first frame after the allocator is
# in one of our files. GEGL counts leaked buffers itself (checked below).
run_checks build-asan "-asan" "ASAN_OPTIONS=detect_leaks=1:exitcode=0 \
  UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 FORENSICS_CHECK_KEEP_MODULES=1"

for op in $ops; do
  log="$out/check-$op-asan.log"
  [ -f "$log" ] || continue
  if grep -qE 'ERROR: AddressSanitizer|runtime error:' "$log"; then
    echo "FAIL  $op: sanitizer errors, see $log:"
    grep -E -A3 'ERROR: AddressSanitizer|runtime error:' "$log" | head -20
    status=1
  else
    echo "PASS  $op: no memory or undefined behavior errors"
  fi
  ours=$(awk '
    function done() { if (block != "" && mine) n++; block = ""; mine = 0 }
    /^(Direct|Indirect) leak/ { done(); block = $0; first = 1; next }
    /^ *#[0-9]+ / && block != "" {
      if (first && $0 !~ / in (malloc|calloc|realloc|g_malloc|g_malloc0|g_realloc|g_try_malloc|g_malloc_n|g_malloc0_n|g_realloc_n|g_strdup|g_strndup|g_strdup_printf|g_strdup_vprintf|g_memdup2|g_array_[a-z_]*|g_slice_[a-z_]*) /) {
        first = 0
        if ($0 ~ /(operations|tests)\/[a-z-]+\.[ch]/) mine = 1
      }
    }
    END { done(); print n + 0 }' "$log")
  all=$(grep -cE '^(Direct|Indirect) leak' "$log" || true)
  if [ "$ours" = 0 ]; then
    echo "PASS  $op: no leaks in our code ($all leaks inside GEGL and its libraries)"
  else
    echo "FAIL  $op: $ours leaks through our code, see $log"
    status=1
  fi
done

if grep -q 'GeglBuffers leaked' "$out"/check-*.log 2>/dev/null; then
  echo "FAIL  GEGL reports leaked buffers:"
  grep -H 'GeglBuffers leaked' "$out"/check-*.log
  status=1
else
  echo "PASS  no leaked GeglBuffers"
fi

snapshot_check "$out/snapshot-check-before.txt" "" || status=1

echo
[ $status = 0 ] && echo "all checks passed" || echo "SOME CHECKS FAILED"
exit $status
