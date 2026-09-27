#!/bin/sh
# The Forensics Workbench in the Flatpak GIMP without a window
# (tests/workbench-check.py), with the operations from build/ (or $BUILD)
# and the plug-in from plug-ins/ in a throwaway GIMP profile
# (tests/output/workbench-profile), isolated from your folders
# (tests/isolate.sh), so that nothing lands in ~/.config/GIMP or
# ~/.var/app/org.gimp.GIMP. Before and after, it lists your folders of
# GIMP and the other apps (gimp-plugin-devtools/snapshot.sh) and fails if
# anything there changed. Exits non-zero if a check fails.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
set -e
here=$(cd "$(dirname "$0")" && pwd)
top=$(dirname "$here")
build=${BUILD:-$top/build}
case $build in /*) ;; *) build=$top/$build ;; esac
out="$here/output/workbench-check"
mod="$here/output/workbench-modules"
profile="$here/output/workbench-profile"
ls "$build"/*.so >/dev/null 2>&1 ||
  { echo "no modules in $build: build first (README)" >&2; exit 2; }
rm -rf "$mod" "$out" "$profile/plug-ins"
mkdir -p "$mod" "$out" "$profile/plug-ins/forensics-workbench"
cp "$build"/*.so "$mod/"
cp "$top/plug-ins/forensics-workbench/forensics-workbench.py" \
  "$profile/plug-ins/forensics-workbench/"
chmod +x "$profile/plug-ins/forensics-workbench/forensics-workbench.py"
src=$top
GIMP_RUN_HOME=${GIMP_RUN_HOME:-$here/output/gimp-home}
export GIMP_RUN_HOME
# shellcheck source=SCRIPTDIR/isolate.sh
. "$here/isolate.sh"
snapshot_take "$here/output/snapshot-workbench-before.txt"

status=0
gimp_run --flatpak --filesystem="$top" \
  --env=GIMP3_DIRECTORY="$profile" \
  --env=GEGL_PATH="$mod:/app/lib/gegl-0.4" \
  --env=FORENSICS_CHECK_OUT="$out" -- \
  gimp-console-3.2 --new-instance --no-interface --no-data --no-fonts \
  --batch-interpreter python-fu-eval \
  -b "exec(open('$here/workbench-check.py').read())" --quit 2>&1 |
  grep -E "^(PASS|FAIL)|Error|Traceback|^  File" || true
[ "$(cat "$out/workbench-check.status" 2>/dev/null)" = 0 ] || status=1

snapshot_check "$here/output/snapshot-workbench-before.txt" "" || status=1
exit $status
