#!/bin/sh
# Tests the Content Credentials plug-in: fetches the C2PA library into the
# plug-in's vendor/ and the test files into tests/content-credentials/images
# if they are missing (the only step that uses the network), makes the
# signed test files, installs the plug-in into a throwaway GIMP profile in
# tests/content-credentials/output (GIMP3_DIRECTORY), then runs
#
#   unit.py      the report without GIMP, in the Python of the GIMP Flatpak
#   gimp-test.py the plug-in in GIMP without a window (gimp-console)
#   gui/gui-test.sh  the dialog on a Broadway display, with screenshots
#                (if a headless Chrome and node are there)
#
# GIMP and its Python run isolated from your own folders (tests/isolate.sh,
# a copy of gimp-devtools/isolate.sh): HOME and the XDG folders
# inside the Flatpak point to tests/content-credentials/output/gimp-home,
# with GIO_USE_VFS=local. Before and after, your folders of GIMP and the
# other apps are listed (gimp-devtools/snapshot.sh), and the test
# fails if anything there changed.
#
#   tests/content-credentials/run.sh
#   CC_GUI=0 tests/content-credentials/run.sh      without the Broadway tests
#   GIMP_FLATPAK=0 tests/content-credentials/run.sh  a native GIMP 3
#
# Prints PASS or FAIL for each check and exits non-zero if one fails.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
here=$(cd "$(dirname "$0")" && pwd)
src=$(dirname "$(dirname "$here")")
plugin=$src/plug-ins/content-credentials
out=$here/output
profile=$out/profile
status=0

GIMP_RUN_HOME=$out/gimp-home
export GIMP_RUN_HOME
# shellcheck source=SCRIPTDIR/../isolate.sh
. "$src/tests/isolate.sh"
mkdir -p "$out"
snapshot_take "$out/snapshot-before.txt"

echo "== dependencies and test files"
python3 "$plugin/fetch-deps.py" || { echo "CC FAIL: no C2PA library"; exit 1; }
python3 "$here/fetch-images.py" || { echo "CC FAIL: test files missing"; exit 1; }

if [ -z "$GIMP_FLATPAK" ]; then
    if command -v flatpak >/dev/null 2>&1 && flatpak info org.gimp.GIMP >/dev/null 2>&1; then
        GIMP_FLATPAK=1
    else
        GIMP_FLATPAK=0
    fi
fi
export GIMP_FLATPAK

# the Python of GIMP (the Flatpak's), isolated
gimp_python () {
    gimp_run --timeout=600 --filesystem="$src" -- python3 "$@"
}

gimp_python "$here/make-fixtures.py" >"$out/make-fixtures.log" 2>&1 ||
  { cat "$out/make-fixtures.log"; echo "CC FAIL: make-fixtures.py"; exit 1; }

# the plug-in, as it would be installed (vendor/ and trust/ with it)
rm -rf "$profile"
mkdir -p "$profile/plug-ins"
cp -r "$plugin" "$profile/plug-ins/content-credentials"
rm -rf "$profile/plug-ins/content-credentials/__pycache__"
chmod 755 "$profile/plug-ins/content-credentials/content-credentials.py"

echo "== unit (Python of GIMP)"
gimp_python "$here/unit.py" >"$out/unit.log" 2>&1
grep -E "^CC|Traceback|Error" "$out/unit.log"
grep -q "^CC unit failures: 0$" "$out/unit.log" || status=1

echo "== GIMP"
if [ "$GIMP_FLATPAK" = 1 ]; then
    console=gimp-console-3.2
else
    console=$(command -v gimp-console-3.2 || command -v gimp-console)
    [ -n "$console" ] || { echo "CC FAIL: no gimp-console on the PATH"; exit 1; }
fi
mkdir -p "$out/gimp"
gimp_run --timeout=1200 --filesystem="$src" \
  --env=GIMP3_DIRECTORY="$profile" --env=CC_SRC="$src" --env=CC_OUT="$out/gimp" -- \
  "$console" --no-interface --no-fonts --batch-interpreter python-fu-eval \
  -b "exec(open('$here/gimp-test.py').read())" --quit >"$out/gimp.log" 2>&1
grep -E "^CC|Traceback|^  File" "$out/gimp.log"
grep -q "^CC GIMP failures: 0$" "$out/gimp.log" || status=1
if grep -E "content-credentials.*(WARNING|CRITICAL)|Traceback" "$out/gimp.log" >/dev/null; then
    echo "CC FAIL: warnings or tracebacks, see $out/gimp.log"
    status=1
fi

if [ "$CC_GUI" != 0 ]; then
    echo "== GUI (Broadway)"
    "$here/gui/gui-test.sh" || status=1
fi

echo "== your folders of GIMP and the other apps"
snapshot_check "$out/snapshot-before.txt" "CC " || status=1

[ $status = 0 ] && echo "CC all passed" || echo "CC FAILED (logs in $out)"
exit $status
