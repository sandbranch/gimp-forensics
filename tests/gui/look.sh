#!/bin/sh
# Opens GIMP with the edited photo on a Broadway display, does the given
# steps and leaves a screenshot after each "shot:name" in
# tests/output/gui/<name>.png; then GIMP is stopped. For looking at the
# filters' dialogs. Build first (README).
#
#   tests/gui/look.sh wait:2000 shot:start key:Escape ...
#
# The steps are those of gimp-devtools/gui/cdp.mjs; positions are
# on the page (FORENSICS_VIEW, 1400,1000 by default); dialog:x,y moves the
# topmost dialog to x, y. The image window is at the top left corner.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=SCRIPTDIR/common.sh
. "$here/common.sh"

wins=$(wait_windows 1)
echo "windows: $wins"
place_image_window
echo "windows: $(windows)"
# the steps, in runs between "dialog:x,y" steps (which move the topmost
# dialog there, move_dialog)
steps=
run_steps () {
    # shellcheck disable=SC2086
    [ -z "$steps" ] || cdp $steps >/dev/null
    steps=
}
for step in "$@"; do
    case $step in
        shot:*) step="shot:$out/${step#shot:}.png" ;;
        dialog:*)
            run_steps
            xy=${step#dialog:}
            move_dialog "${xy%,*}" "${xy#*,}" >/dev/null
            continue ;;
    esac
    steps="$steps $step"
done
run_steps
echo "windows: $(windows)"
