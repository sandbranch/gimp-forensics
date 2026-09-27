#!/bin/sh
# Tests the JPEG Info plug-in (plug-ins/jpeg-info): makes the fixtures in
# headless GIMP (make-fixtures.py), runs unit.py (the report, in the
# Python of the GIMP Flatpak; its coefficient decoder against libjpeg
# through build/tests/jpeg-coefficients) and gimp-test.py (the plug-in in
# GIMP without a window), with the plug-in in a throwaway profile
# (tests/output/jpeg-info/profile), isolated from your folders
# (tests/isolate.sh). Needs no network; build first (README). Before and
# after, your folders of GIMP and the other apps are listed and must be
# the same. Exits non-zero if a check fails.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
here=$(cd "$(dirname "$0")" && pwd)
tests=$(dirname "$here")
src=$(dirname "$tests")
out=$tests/output/jpeg-info
profile=$out/profile
build=${BUILD:-$src/build}
case $build in /*) ;; *) build=$src/$build ;; esac
status=0
GIMP_RUN_HOME=${GIMP_RUN_HOME:-$tests/output/gimp-home}
export GIMP_RUN_HOME
# shellcheck source=SCRIPTDIR/../isolate.sh
. "$tests/isolate.sh"
snapshot_take "$tests/output/snapshot-jpeg-info-before.txt"
[ -x "$build/tests/jpeg-coefficients" ] ||
  { echo "FAIL  no $build/tests/jpeg-coefficients: build first (README)"; exit 1; }

rm -rf "$out"
mkdir -p "$out" "$profile/plug-ins/jpeg-info"
cp "$src"/plug-ins/jpeg-info/*.py "$src"/plug-ins/jpeg-info/*.tsv "$profile/plug-ins/jpeg-info/"
chmod +x "$profile/plug-ins/jpeg-info/jpeg-info.py"

console () {
    gimp_run --timeout=900 --flatpak --filesystem="$src" \
      --env=GIMP3_DIRECTORY="$profile" --env=JI_OUT="$out" --env=JI_SRC="$src" -- \
      gimp-console-3.2 --new-instance --no-interface --no-data --no-fonts \
      --batch-interpreter python-fu-eval -b "exec(open('$1').read())" --quit
}

console "$here/make-fixtures.py" >"$out/make-fixtures.log" 2>&1
grep -q "^JI fixtures written" "$out/make-fixtures.log" ||
  { cat "$out/make-fixtures.log"; echo "FAIL  make-fixtures.py"; exit 1; }

gimp_run --timeout=900 --flatpak --filesystem="$src" --env=JI_OUT="$out" --env=JI_SRC="$src" \
  --env=JI_COEFS="$build/tests/jpeg-coefficients" -- python3 "$here/unit.py" >"$out/unit.log" 2>&1
grep -E "^(PASS|FAIL)|Traceback|Error" "$out/unit.log"
grep -q "^JI unit failures: 0$" "$out/unit.log" || status=1

console "$here/gimp-test.py" >"$out/gimp.log" 2>&1
grep -E "^(PASS|FAIL)|Traceback|^  File" "$out/gimp.log"
grep -q "^JI GIMP failures: 0$" "$out/gimp.log" || status=1
if grep -E "jpeg-info.*(WARNING|CRITICAL)|Traceback" "$out/gimp.log" >/dev/null; then
    echo "FAIL  warnings or tracebacks, see $out/gimp.log"
    status=1
fi

snapshot_check "$tests/output/snapshot-jpeg-info-before.txt" "" || status=1
exit $status
