# Sourced by look.sh and gui-test.sh: starts a headless Chrome on a free
# port, opens the Broadway page, then GIMP (start.sh) with the edited
# photo; waits until GIMP's image window is there. Sets $here, $out,
# $cdp (cdp.mjs with the page size), and stops the Chrome and the GIMP it
# started on exit (only those: the GIMP is found by its script).
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
# ($here is set by the script that sources this one)
# shellcheck disable=SC2154
tests=$(dirname "$here")
src=$(dirname "$tests")
out=$tests/output/gui
devtools=${GIMP_PLUGIN_DEVTOOLS:-$src/../gimp-plugin-devtools}
view=${FORENSICS_VIEW:-1400,1000}

chrome=$(command -v google-chrome || command -v chromium || command -v chromium-browser)
[ -n "$chrome" ] || { echo "SKIP  gui: no Chrome or Chromium"; exit 0; }
command -v node >/dev/null || { echo "SKIP  gui: no node"; exit 0; }

free_port () {
    python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])'
}
mkdir -p "$out"
rm -f "$out/page-open"
CDP_PORT=$(free_port)
export CDP_PORT
bw_port=$(free_port)
bw_display=$(( (bw_port % 50) + 30 ))
FORENSICS_BROADWAY=$bw_port:$bw_display
export FORENSICS_BROADWAY
# the page size is set in each call: Chrome forgets it when cdp.mjs ends
cdp () { node "$devtools/gui/cdp.mjs" "size:$view" "$@"; }

ours () {
    flatpak ps --columns=instance,child-pid,application 2>/dev/null |
      while read -r instance pid app; do
          [ "$app" = org.gimp.GIMP ] || continue
          tr '\0' ' ' 2>/dev/null < "/proc/$pid/cmdline" |
            grep -qF "$here/open-image.py" && echo "$instance"
      done
}
[ -z "$(ours)" ] || { echo "FAIL  gui: the test's GIMP is already running"; exit 1; }
chrome_pid=
gimp_pid=
cleanup () {
    for instance in $(ours); do
        flatpak kill "$instance" 2>/dev/null
    done
    [ -n "$gimp_pid" ] && kill "$gimp_pid" 2>/dev/null
    if [ -n "$chrome_pid" ]; then
        kill "$chrome_pid" 2>/dev/null
        wait "$chrome_pid" 2>/dev/null
        sleep 1
    fi
    rm -rf "$out/chrome"
}
trap cleanup EXIT
# (dash runs the EXIT trap only on a normal exit)
trap 'exit 1' INT TERM HUP

# (the window size stays when cdp.mjs ends; GIMP sizes its windows for it)
"$chrome" --headless=new --remote-debugging-port="$CDP_PORT" \
  --window-size="$view" \
  --user-data-dir="$out/chrome" --password-store=basic about:blank \
  >/dev/null 2>&1 &
chrome_pid=$!
"$here/start.sh" >"$out/gimp.log" 2>&1 &
gimp_pid=$!
# the page first (broadwayd is up two seconds after start.sh)
i=0
until cdp nav:"http://127.0.0.1:$bw_port/" wait:1000 >/dev/null 2>&1 || [ $i -ge 30 ]; do
    sleep 1
    i=$((i + 1))
done
touch "$out/page-open"

# the windows on the page: Broadway draws each as a canvas, stacked by
# z-index. windows prints "x,y,w,h" per window, topmost first.
windows () {
    cdp nav:"http://127.0.0.1:$bw_port/" wait:1500 \
      "eval:(() => [...document.querySelectorAll('canvas')]
        .map(e => [e.getBoundingClientRect(), Number(e.style.zIndex) || 0])
        .filter(([r]) => r.width > 150 && r.height > 100)
        .sort((a, b) => b[1] - a[1])
        .map(([r]) => [r.left, r.top, r.width, r.height].map(Math.round).join(','))
        .join(' '))()" 2>/dev/null
}
# moves GIMP's image window (the one wider than 1000 pixels) to the top
# left corner of the page: GIMP puts it above and left of the page on
# Broadway (start.sh). The page's Broadway
# client moves the window and tells the server (a configure notify), as
# when a window is moved in the browser; GIMP then takes the new place.
unhide_windows () {
    cdp nav:"http://127.0.0.1:$bw_port/" wait:1500 \
      "eval:(() => { let n = 0; for (const s of Object.values(surfaces))
         if (s.visible && !s.isTemp && s.width > 1000 && (s.x != 0 || s.y != 0)) {
           cmdMoveResizeSurface(s.id, true, 0, 0, false, 0, 0);
           n++; }
         return n; })()" 2>/dev/null
}
# waits until there are at least n windows (GIMP's image window is one)
wait_windows () {
    ww_n=$1
    i=0
    while [ $i -lt 60 ]; do
        w=$(windows)
        [ "$(echo "$w" | wc -w)" -ge "$ww_n" ] && break
        i=$((i + 1))
        sleep 2
    done
    echo "$w"
}

# the image window at the top left corner, where it stays: sets x0 and y0
# (the corner of its canvas on the page; GTK draws a shadow around it)
place_image_window () {
    i=0
    stable=0
    while [ $i -lt 20 ] && [ $stable -lt 2 ]; do
        moved=$(unhide_windows)
        sleep 2
        if [ "$moved" = 0 ]; then stable=$((stable + 1)); else stable=0; fi
        i=$((i + 1))
    done
    x0=0
    y0=0
}
# p dx dy: a position from the image window's corner
p () { echo "$(( x0 + $1 )),$(( y0 + $2 ))"; }
# move_dialog x y: moves the topmost window smaller than the image window
# (a dialog) so that its corner is at x, y on the page, off the image
move_dialog () {
    cdp nav:"http://127.0.0.1:$bw_port/" wait:1500 \
      "eval:(() => { const d = Object.values(surfaces)
         .filter(s => s.visible && !s.isTemp && s.width < 1000)
         .sort((a, b) => stackingOrder.indexOf(b) - stackingOrder.indexOf(a))[0];
         if (!d) return 0;
         cmdMoveResizeSurface(d.id, true, $1, $2, false, 0, 0); return 1; })()" 2>/dev/null
}
