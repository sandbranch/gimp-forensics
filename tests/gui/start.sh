#!/bin/sh
# Opens the Flatpak GIMP on a Broadway display with a synthetic edited
# photo (open-image.py), with the operations from build/ (or $BUILD) and
# the plug-ins of this repository in a throwaway profile, isolated from
# your folders (tests/isolate.sh). For looking at the filters in GIMP's
# own dialogs with gimp-devtools/gui/cdp.mjs (look.sh does that).
#
#   tests/gui/start.sh              http://127.0.0.1:8085/ (display :5)
#   FORENSICS_BROADWAY=8091:11 tests/gui/start.sh
#
# GIMP starts when tests/output/gui/page-open exists: GIMP places its
# window for the size of the Broadway screen, which is that of the page
# once a browser shows it (common.sh does so first). broadwayd stops when
# GIMP quits, also when GIMP fails. GIMP loads no
# fonts (--no-fonts): on Broadway it often hung at start while loading
# them. --new-instance: without it GIMP hands the script over to a GIMP
# that is already running (yours, or another test's) and quits.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
here=$(cd "$(dirname "$0")" && pwd)
tests=$(dirname "$here")
src=$(dirname "$tests")
out=$tests/output/gui
build=${BUILD:-$src/build}
case $build in /*) ;; *) build=$src/$build ;; esac
bw=${FORENSICS_BROADWAY:-8085:5}
port=${bw%:*}
display=${bw#*:}
mod=$tests/output/gui-modules
profile=$tests/output/gui-profile
mkdir -p "$out" "$mod" "$profile/plug-ins"
# no Welcome dialog: GIMP shows it when the profile is of an older
# version, or when asked to
if ! grep -q config-version "$profile/gimprc" 2>/dev/null; then
    version=$(LC_ALL=C flatpak info org.gimp.GIMP | sed -n 's/^ *Version: *//p')
    printf '(config-version "%s")\n' "$version" >> "$profile/gimprc"
fi
grep -q show-welcome-dialog "$profile/gimprc" 2>/dev/null ||
  echo '(show-welcome-dialog no)' >> "$profile/gimprc"
# one image window filling the page, with the layers on the right. GIMP
# clamps a window's position into the monitor's work area, which is 0 x 0
# on Broadway, so that any position becomes minus the window's size
# (above and left of the page). With CLAMP (x, 0, -width), a position
# below -width comes out as 0.
view=${FORENSICS_VIEW:-1400,1000}
cat >"$profile/sessionrc" <<SESSION
(session-info "toplevel"
    (factory-entry "gimp-single-image-window")
    (position -100000 -100000)
    (size $((${view%,*} - 60)) $((${view#*,} - 60)))
    (aux-info
        (left-docks-width "1")
        (right-docks-width "300")
        (maximized "no"))
    (gimp-dock
        (side right)
        (book
            (current-page 0)
            (dockable "gimp-layer-list"
                (tab-style icon)
                (preview-size 32)))))
SESSION
rm -f "$mod"/*.so
cp "$build"/*.so "$mod/"
# the plug-ins, as GIMP finds them in a profile (a folder each)
for p in "$src"/plug-ins/*/; do
    name=$(basename "$p")
    [ -f "$p/$name.py" ] || continue
    rm -rf "${profile:?}/plug-ins/$name"
    mkdir -p "$profile/plug-ins/$name"
    cp "$p"/*.py "$profile/plug-ins/$name/"
    chmod +x "$profile/plug-ins/$name/$name.py"
done
GIMP_RUN_HOME=${GIMP_RUN_HOME:-$tests/output/gimp-home}
export GIMP_RUN_HOME
# shellcheck source=SCRIPTDIR/../isolate.sh
. "$tests/isolate.sh"
gimp_run --flatpak --filesystem="$src" \
  --env=GDK_BACKEND=broadway --env=BROADWAY_DISPLAY=":$display" \
  --env=GIMP3_DIRECTORY="$profile" \
  --env=GEGL_PATH="$mod:/app/lib/gegl-0.4" \
  --env=FORENSICS_GUI_OUT="$out" -- sh -c \
  "broadwayd --port $port :$display & bw=\$!; trap 'kill \$bw' EXIT; \
   i=0; while [ ! -f '$out/page-open' ] && [ \$i -lt 300 ]; do sleep 0.2; i=\$((i+1)); done; sleep 3; \
   gimp-3.2 --new-instance --no-splash --no-fonts \
   --batch-interpreter python-fu-eval -b \"exec(open('$here/open-image.py').read())\""
