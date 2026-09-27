#!/bin/sh
# Runs every test of the repository, in this order:
#
#   1. shellcheck of the scripts (skipped without shellcheck)
#   2. tests/check.sh: the operations' checks, as usual and under
#      AddressSanitizer and UBSan
#   3. the build in build/ (set up if it is missing), then
#      tests/gimp-check.sh: the filters in headless GIMP, XCF, the gegl
#      command line
#   4. tests/workbench-check.sh: the Forensics Workbench in headless GIMP
#   5. tests/gui/gui-test.sh: the dialogs on Broadway (skipped with
#      FORENSICS_NO_GUI=1, or without Chrome, node or ImageMagick)
#   6. the tests of each plug-in folder that has its own
#      plug-ins/<name>/tests/run.sh
#
# Everything runs isolated from your folders (tests/isolate.sh); each part
# checks that your folders of GIMP and the other apps did not change, and
# so does this script around all of it. Needs no network. Exits non-zero
# if anything failed, after running the rest.
#
#   tests/run-all.sh
#   FORENSICS_NO_GUI=1 tests/run-all.sh
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
here=$(cd "$(dirname "$0")" && pwd)
top=$(dirname "$here")
src=$top
out=$here/output
mkdir -p "$out"
GIMP_RUN_HOME=${GIMP_RUN_HOME:-$out/gimp-home}
export GIMP_RUN_HOME
# shellcheck source=SCRIPTDIR/isolate.sh
. "$here/isolate.sh"
snapshot_take "$out/snapshot-all-before.txt"

status=0
failed=
part () {
    name=$1
    shift
    echo
    echo "=== $name"
    if "$@"; then
        :
    else
        status=1
        failed="$failed $name"
    fi
}

if command -v shellcheck >/dev/null 2>&1; then
    part shellcheck shellcheck -s sh -x "$here"/*.sh "$here"/gui/*.sh
else
    echo "=== shellcheck: SKIP (not installed)"
fi

part check "$here/check.sh"

gimp_build=${GIMP_BUILD:-$top/../gimp-plugin-devtools/gimp-build.sh}
if [ ! -f "$top/build/build.ninja" ]; then
    if [ "${GIMP_FLATPAK:-1}" = 1 ] && [ -x "$gimp_build" ]; then
        "$gimp_build" "$top" meson setup build >/dev/null
    else
        (cd "$top" && meson setup build >/dev/null)
    fi
fi
if [ "${GIMP_FLATPAK:-1}" = 1 ] && [ -x "$gimp_build" ]; then
    part build "$gimp_build" "$top" ninja -C build
else
    part build ninja -C "$top/build"
fi

part gimp-check "$here/gimp-check.sh"
part workbench-check "$here/workbench-check.sh"
if [ -n "${FORENSICS_NO_GUI:-}" ]; then
    echo
    echo "=== gui: SKIP (FORENSICS_NO_GUI)"
else
    part gui "$here/gui/gui-test.sh"
fi

for t in "$top"/plug-ins/*/tests/run.sh; do
    [ -x "$t" ] || continue
    plugin=$(basename "$(dirname "$(dirname "$t")")")
    part "$plugin" "$t"
done

echo
snapshot_check "$out/snapshot-all-before.txt" "" || { status=1; failed="$failed snapshot"; }
if [ $status = 0 ]; then
    echo "ALL TESTS PASSED"
else
    echo "FAILED:$failed"
fi
exit $status
