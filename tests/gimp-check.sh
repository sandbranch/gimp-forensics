#!/bin/sh
# The operations as non-destructive filters in the Flatpak GIMP, without a
# window (tests/gimp-check.py): on float, 8 and 16 bit images, through an
# XCF save and load, against plain GEGL; Error Level Analysis against
# GIMP's own JPEG export; then GIMP's results against the gegl command
# line. Uses the modules from build/ (or $BUILD) and a throwaway GIMP
# profile in tests/output/gimp-profile (GIMP3_DIRECTORY), so the installed
# filters and the user's GIMP settings are not touched. The first run
# takes about a minute (GIMP sets up the new profile). GIMP and GEGL run
# isolated from your folders (tests/isolate.sh, with
# gimp-plugin-devtools/gimp-run.sh if it is there): HOME and the XDG
# folders inside the Flatpak point into tests/output/gimp-home, so
# nothing lands in ~/.var/app/org.gimp.GIMP.
# Before and after, it lists your folders of GIMP and the other apps
# (gimp-plugin-devtools/snapshot.sh, skipped without it) and fails if
# anything there changed. Exits non-zero if a check fails.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
set -e
here=$(cd "$(dirname "$0")" && pwd)
top=$(dirname "$here")
build=${BUILD:-$top/build}
case $build in /*) ;; *) build=$top/$build ;; esac
out="$here/output/gimp-check"
mod="$here/output/gimp-check-modules"
ls "$build"/*.so >/dev/null 2>&1 ||
  { echo "no modules in $build: build first (README)" >&2; exit 2; }
rm -rf "$mod" "$out"
mkdir -p "$mod" "$out" "$here/output/gimp-profile"
src=$top
GIMP_RUN_HOME=${GIMP_RUN_HOME:-$here/output/gimp-home}
export GIMP_RUN_HOME
# shellcheck source=SCRIPTDIR/isolate.sh
. "$here/isolate.sh"
snapshot_take "$here/output/snapshot-gimp-before.txt"
# GEGL loads every file in a module folder: only the modules
cp "$build"/*.so "$mod/"

# run <command...>: in the GIMP Flatpak
run () {
  gimp_run --flatpak --filesystem="$top" \
    --env=GIMP3_DIRECTORY="$here/output/gimp-profile" \
    --env=GEGL_PATH="$mod:/app/lib/gegl-0.4" \
    --env=FORENSICS_CHECK_OUT="$out" -- "$@"
}

status=0
run gimp-console-3.2 --new-instance --no-interface --no-data --no-fonts \
  --batch-interpreter python-fu-eval \
  -b "exec(open('$here/gimp-check.py').read())" --quit 2>&1 |
  grep -E "^(PASS|FAIL)|failed$|Error|Traceback|^  File" || true
[ "$(cat "$out/gimp-check.status" 2>/dev/null)" = 0 ] || status=1

# the same settings on the gegl command line
while read -r label op props; do
  # (the gegl command line itself reports a leaked GeglBuffer for area
  # filters, gegl:gaussian-blur too: its messages go to a log)
  # shellcheck disable=SC2086
  run gegl "$out/scene.tif" -o "$out/cli-$label.tif" -- "$op" $props \
    >>"$out/gegl-cli.log" 2>&1
done <"$out/cli-cases.txt"

run python3 - "$out" <<'EOF' || status=1
import array, sys
import gi
gi.require_version('Gegl', '0.4')
from gi.repository import Gegl

out = sys.argv[1]
Gegl.init(None)
bad = 0

def load(path):
    g = Gegl.Node()
    n = g.create_child('gegl:tiff-load')
    n.set_property('path', path)
    r = n.get_bounding_box()
    buf = Gegl.Buffer.new("R'G'B'A float", r.x, r.y, r.width, r.height)
    w = g.create_child('gegl:write-buffer')
    w.set_property('buffer', buf)
    n.link(w)
    w.process()
    return array.array('f', buf.get(r, 1.0, "R'G'B'A float", Gegl.AbyssPolicy.NONE))

for line in open(out + '/cli-cases.txt'):
    label = line.split()[0]
    a = load(out + '/gimp-' + label + '.tif')
    b = load(out + '/cli-' + label + '.tif')
    d = max(abs(p - q) for p, q in zip(a, b)) if len(a) == len(b) and a else float('inf')
    ok = d <= 1e-5
    bad += not ok
    print('%s  gimp_%s_same_as_gegl_command_line: max difference %.2g'
          % ('PASS' if ok else 'FAIL', label, d))
raise SystemExit(bad)
EOF

snapshot_check "$here/output/snapshot-gimp-before.txt" "" || status=1
exit $status
