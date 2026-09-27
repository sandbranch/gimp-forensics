#!/bin/sh
# Tests the Content Credentials dialog on a Broadway display, with a
# headless Chrome: for each of a few test files, GIMP opens it, the dialog
# opens, a screenshot of it is left in tests/content-credentials/output/gui
# (<name>.png, and <name>-page.png of the whole page), Close is clicked,
# and the procedure must end with success and the report's status for that
# file. Run tests/content-credentials/run.sh first (it installs the
# plug-in and makes the test files); it runs this too.
#
#   tests/content-credentials/gui/gui-test.sh
#   CC_GUI_ONLY="ocsp none" tests/content-credentials/gui/gui-test.sh
#
# Needs a headless Chrome (google-chrome or chromium), node 22 and
# ../gimp-devtools (or GIMP_PLUGIN_DEVTOOLS) for gui/cdp.mjs.
# Prints PASS or FAIL for each check and exits non-zero if one fails.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
here=$(cd "$(dirname "$0")" && pwd)
status=0
pass () { echo "CC GUI PASS $1"; }
fail () { echo "CC GUI FAIL $1"; status=1; }
CC_GUI_NAME=
export CC_GUI_FILE CC_GUI_NAME

# shellcheck source=SCRIPTDIR/common.sh
. "$here/common.sh"
images=$tests/images
rm -f "$out"/*.png

# name, file, the report's status
SESSIONS="ocsp:c2pa-rs-ocsp.jpg:trusted
tampered:adobe-20220124-E-dat-CA.jpg:tampered
none:adobe-20220124-A.jpg:none
ingredients:adobe-20220124-CACAICAICICA.jpg:untrusted
xmp:generated/xmp-ai.jpg:none
edited:generated/ai-edited.jpg:untrusted"

# the Close button, from the dialog's bottom right corner (the canvas
# includes the window's shadow)
CLOSE_FROM_RIGHT=${CC_CLOSE_FROM_RIGHT:-99}
CLOSE_FROM_BOTTOM=${CC_CLOSE_FROM_BOTTOM:-60}

session () {
    name=$1 file=$2 want=$3
    CC_GUI_NAME=$name
    CC_GUI_FILE=$images/$file
    start_gimp
    find_dialog
    if [ -z "$at" ]; then
        fail "$name: the dialog did not open (log: $out/gimp-$name.log)"
        stop_all
        return
    fi
    pass "$name: the dialog opened ($dw x $dh)"
    if [ $((y0 + dh)) -le 1000 ] && [ $((x0 + dw)) -le 1400 ]; then
        pass "$name: the dialog fits a 1400 x 1000 screen"
    else
        fail "$name: the dialog does not fit a 1400 x 1000 screen (at $x0,$y0)"
    fi
    $cdp "$view" wait:1000 shot:"$out/$name-page.png" >/dev/null
    python3 - "$out/$name-page.png" "$out/$name.png" "$x0" "$y0" "$dw" "$dh" <<'PY'
import sys
from PIL import Image
page, dialog, x, y, w, h = sys.argv[1:]
x, y, w, h = int(x), int(y), int(w), int(h)
Image.open(page).crop((x, y, x + w, y + h)).save(dialog)
PY
    $cdp "$view" click:"$(p $((dw - CLOSE_FROM_RIGHT)) $((dh - CLOSE_FROM_BOTTOM)))" >/dev/null
    if ! wait_for "$out/result.txt" 30; then
        fail "$name: Close did not close the dialog (log: $out/gimp-$name.log)"
        stop_all
        return
    fi
    if grep -q '^status success$' "$out/result.txt"; then
        pass "$name: closed, status success"
    else
        fail "$name: $(head -1 "$out/result.txt")"
    fi
    if grep -q "^report $want\$" "$out/result.txt"; then
        pass "$name: report status $want"
    else
        fail "$name: $(sed -n 2p "$out/result.txt"), expected $want"
    fi
    stop_all
}

for line in $SESSIONS; do
    name=${line%%:*} rest=${line#*:}
    file=${rest%%:*} want=${rest#*:}
    if [ -n "$CC_GUI_ONLY" ] && ! echo " $CC_GUI_ONLY " | grep -q " $name "; then
        continue
    fi
    session "$name" "$file" "$want"
done

[ $status = 0 ] && echo "CC GUI all passed (screenshots in $out)" ||
  echo "CC GUI FAILED (screenshots in $out)"
exit $status
