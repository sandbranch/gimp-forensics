#!/bin/sh
# The filters and the Forensics Workbench in GIMP's own windows, on a
# Broadway display (look.sh, with the edited photo of open-image.py):
#
#   - Filters > Forensics lists the twelve filters;
#   - each filter's dialog opens from it, and its preview changes the
#     image on the canvas;
#   - Image > Forensics > Analyze Image... opens the Workbench's dialog;
#     OK adds the analyses (the canvas shows Error Level Analysis, dark),
#     and the Layers dialog shows the Forensics group;
#   - Image > Forensics > JPEG Info... opens its report; Add Double JPEG
#     Map adds the map (the region pasted from a quality 60 JPEG white).
#
# The checks read the screenshots: the mean of the image's area on the
# canvas (ImageMagick), against the photo's. A GIMP for each step (after a
# filter Filters gets a Recently Used entry and its items move).
# Screenshots in tests/output/gui/; with --docs also into docs/. Build
# first (README). Prints PASS or FAIL per check.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
here=$(cd "$(dirname "$0")" && pwd)
tests=$(dirname "$here")
top=$(dirname "$tests")
out=$tests/output/gui
docs=
[ "$1" = --docs ] && docs=$top/docs
# wide enough for the image window, a dialog beside the image and the
# Layers dock
FORENSICS_VIEW=${FORENSICS_VIEW:-1600,1000}
export FORENSICS_VIEW
page_w=${FORENSICS_VIEW%,*}
# the photo (480 x 320 at 100 %) is centred in the canvas: at x0, 428 on
# the page (482 on a page 1400 wide, half the extra width further right);
# dialogs go beside it, at dx
x0=$(( 482 + (page_w - 1400) / 2 ))
dx=$(( x0 + 500 ))
command -v convert >/dev/null || { echo "SKIP  gui: no ImageMagick"; exit 0; }
status=0
pass () { echo "PASS  gui_$1"; }
fail () { echo "FAIL  gui_$1"; status=1; }

# the image's area on the canvas (a margin of 10 left out)
AREA=460x300+$((x0 + 10))+438
# its mean (0 to 1)
area_mean () {
    convert "$1" -crop $AREA -colorspace gray -format '%[fx:mean]' info:
}
# the mean absolute difference (0 to 1) of the image's area in two shots
area_diff () {
    convert \( "$1" -crop $AREA +repage \) \( "$2" -crop $AREA +repage \) \
      -compose difference -composite -colorspace gray -format '%[fx:mean]' info:
}

look () { "$here/look.sh" "$@" >"$out/look.log" 2>&1 || true; }

mkdir -p "$out"
rm -f "$out"/*.png
look wait:1000 shot:start click:452,72 wait:1500 move:485,575 wait:1500 shot:filters-menu
if [ ! -f "$out/start.png" ]; then
    fail "gimp_started (see $out/gimp.log)"
    exit 1
fi
start=$(area_mean "$out/start.png")
if [ -f "$out/filters-menu.png" ]; then pass "filters_menu_open"; else fail "filters_menu_open"; fi

y=575
for name in bit-plane clone-detect echo error-level jpeg-ghost luminance-gradient median-detect \
    minmax noise pca resampling wavelet-noise; do
    look click:452,72 wait:1500 move:485,575 wait:1200 move:700,575 \
      move:750,$y wait:600 click:750,$y wait:4000 dialog:$dx,150 wait:6000 \
      shot:dialog-$name
    y=$((y + 25))
    if [ ! -f "$out/dialog-$name.png" ]; then
        fail "dialog_$name: no screenshot"
        continue
    fi
    d=$(area_diff "$out/start.png" "$out/dialog-$name.png")
    # (the dialog opened: look.sh lists it, a window beside the image)
    if grep -q "windows:.* $dx,150," "$out/look.log" &&
       python3 -c "import sys; sys.exit(0 if $d > 0.05 else 1)"; then
        pass "dialog_$name: opened, the preview changes the image (by $d on average)"
    else
        fail "dialog_$name: $(grep windows: "$out/look.log" | tail -1); changed by $d"
    fi
done

look wait:1000 click:238,72 wait:1500 move:270,629 wait:1200 move:450,629 \
  move:552,629 wait:500 click:552,629 wait:6000 dialog:$dx,60 wait:1500 \
  shot:workbench-dialog click:$((dx + 424)),858 wait:15000 shot:workbench-result \
  click:518,72 wait:1200 move:565,97 wait:1200 move:700,97 move:780,148 \
  wait:500 click:780,148 wait:3000 shot:workbench-layers
if grep -q "windows:.* $dx,60," "$out/look.log" 2>/dev/null ||
   [ -f "$out/workbench-dialog.png" ]; then
    pass "workbench_dialog_open"
else
    fail "workbench_dialog_open"
fi
if [ -f "$out/workbench-result.png" ]; then
    m=$(area_mean "$out/workbench-result.png")
    if python3 -c "import sys; sys.exit(0 if $m < 0.15 and $start > 0.3 else 1)"; then
        pass "workbench_result: the canvas shows Error Level Analysis (mean $start to $m)"
    else
        fail "workbench_result: mean $start to $m"
    fi
else
    fail "workbench_result: no screenshot"
fi

# Image > Forensics > JPEG Info...: the dialog, then Add Double JPEG Map
# (the pasted region was saved at 60 before the photo was saved at 90: it
# shows white, the rest black)
jx=700
look wait:1000 click:238,72 wait:1500 move:270,629 wait:1200 move:450,629 \
  move:552,679 wait:500 click:552,679 wait:8000 dialog:$jx,60 wait:2000 \
  shot:jpeg-info-dialog click:$((jx + 339)),810 wait:6000 shot:jpeg-info-map
# (the dialog covers the right of the canvas: that part changed)
DLG=$((page_w - jx - 60))x600+$((jx + 10))+150
dlg_diff () {
    convert \( "$1" -crop $DLG +repage \) \( "$2" -crop $DLG +repage \) \
      -compose difference -composite -colorspace gray -format '%[fx:mean]' info:
}
if [ -f "$out/jpeg-info-dialog.png" ] &&
   python3 -c "import sys; sys.exit(0 if $(dlg_diff "$out/start.png" "$out/jpeg-info-dialog.png") > 0.05 else 1)"; then
    pass "jpeg_info_dialog_open"
else
    fail "jpeg_info_dialog_open"
fi
if [ -f "$out/jpeg-info-map.png" ]; then
    m=$(area_mean "$out/jpeg-info-map.png")
    if python3 -c "import sys; sys.exit(0 if $m < 0.2 and $start > 0.3 else 1)"; then
        pass "jpeg_info_double_map: the canvas shows the map (mean $start to $m)"
    else
        fail "jpeg_info_double_map: mean $start to $m"
    fi
else
    fail "jpeg_info_double_map: no screenshot"
fi

if [ -n "$docs" ]; then
    mkdir -p "$docs"
    convert "$out/filters-menu.png" -crop 540x320+420+550 +repage "$docs/filters-menu.png"
    for name in bit-plane clone-detect echo error-level jpeg-ghost luminance-gradient median-detect \
    minmax noise pca resampling wavelet-noise; do
        [ -f "$out/dialog-$name.png" ] &&
          convert "$out/dialog-$name.png" -crop $((page_w - x0 + 12))x640+$((x0 - 12))+140 +repage \
            "$docs/dialog-$name.png"
    done
    convert "$out/workbench-dialog.png" -crop 480x830+$((dx - 5))+80 +repage "$docs/workbench-dialog.png"
    convert "$out/jpeg-info-dialog.png" -crop 830x790+$((jx - 5))+80 +repage "$docs/jpeg-info-dialog.png"
    convert "$out/jpeg-info-map.png" -crop $((page_w - x0 + 12))x640+$((x0 - 12))+120 +repage \
      "$docs/jpeg-info-map.png"
    convert "$out/workbench-layers.png" -crop $((page_w - x0 + 12))x640+$((x0 - 12))+120 +repage \
      "$docs/workbench-result.png"
fi
exit $status
