# shellcheck shell=sh
# Sourced by gui-test.sh (from gimp-layerfx's tests): starts GIMP
# (start.sh, with CC_GUI_FILE and CC_GUI_NAME) and a headless Chrome
# looking at the Broadway page. Sets $here, $out, $cdp, $view; start_gimp
# starts both, find_dialog sets x0, y0, dw and dh (the dialog on the page),
# p gives positions from its top left corner. Stops the Chrome and the GIMP
# it started on exit, and only those.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
# ($here is set by the script that sources this one)
# shellcheck disable=SC2154
tests=$(dirname "$here")
src=$(dirname "$(dirname "$tests")")
out=$tests/output/gui
devtools=${GIMP_PLUGIN_DEVTOOLS:-$src/../gimp-plugin-devtools}
cdp="node $devtools/gui/cdp.mjs"
view=size:${CC_VIEW:-1400,1000}

chrome=$(command -v google-chrome || command -v chromium || command -v chromium-browser)
[ -n "$chrome" ] || { echo "CC GUI SKIP: no Chrome or Chromium"; exit 0; }
command -v node >/dev/null 2>&1 || { echo "CC GUI SKIP: no node"; exit 0; }
[ -f "$devtools/gui/cdp.mjs" ] || { echo "CC GUI SKIP: no $devtools/gui/cdp.mjs"; exit 0; }
[ -x "$tests/output/profile/plug-ins/content-credentials/content-credentials.py" ] ||
  { echo "CC GUI FAIL: run tests/content-credentials/run.sh first (it installs the plug-in)"; exit 1; }
mkdir -p "$out"

# the Flatpak instance of our GIMP: the one whose sandbox runs
# gui-script.py. Only it is stopped at the end.
ours () {
    flatpak ps --columns=instance,child-pid,application 2>/dev/null |
      while read -r instance pid app; do
          [ "$app" = org.gimp.GIMP ] || continue
          tr '\0' ' ' 2>/dev/null < "/proc/$pid/cmdline" |
            grep -qF "$here/gui-script.py" && echo "$instance"
      done
}
[ -z "$(ours)" ] || { echo "CC GUI FAIL: the test's GIMP is already running"; exit 1; }
chrome_pid=
gimp_instance=
stop_all () {
    if [ -n "$chrome_pid" ]; then
        kill "$chrome_pid" 2>/dev/null
        wait "$chrome_pid" 2>/dev/null
        sleep 1
        rm -rf "$out/chrome.$$"
    fi
    chrome_pid=
    [ -n "$gimp_instance" ] || gimp_instance=$(ours)
    for instance in $gimp_instance; do
        flatpak kill "$instance" 2>/dev/null
    done
    gimp_instance=
    # wait for it to go
    i=0
    while [ -n "$(ours)" ] && [ $i -lt 20 ]; do sleep 1; i=$((i + 1)); done
}
trap stop_all EXIT
# (dash runs the EXIT trap only on a normal exit)
trap 'exit 1' INT TERM HUP

# a free port for Broadway, 8087 to 8179 (the display is the port minus
# 8080): a port can be taken by another program, or stay taken for a
# while after a run (TIME_WAIT)
free_broadway_port () {
    python3 -c '
import socket
for port in range(8087, 8180):
    try:
        s = socket.socket(socket.AF_INET6)
        s.bind(("::", port))
        s.close()
        print(port)
        break
    except OSError:
        pass'
}

start_gimp () {
    CC_BROADWAY_PORT=$(free_broadway_port)
    export CC_BROADWAY_PORT
    page=http://127.0.0.1:$CC_BROADWAY_PORT/
    rm -f "$out/result.txt" "$out/page-open"
    "$here/start.sh" >"$out/gimp-$CC_GUI_NAME.log" 2>&1 </dev/null &
    CDP_PORT=$(python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')
    export CDP_PORT
    "$chrome" --headless=new --remote-debugging-port="$CDP_PORT" \
      --window-size="${CC_VIEW:-1400,1000}" \
      --user-data-dir="$out/chrome.$$" --password-store=basic about:blank \
      >/dev/null 2>&1 &
    chrome_pid=$!
    i=0
    while [ -z "$gimp_instance" ] && [ $i -lt 30 ]; do
        sleep 1
        gimp_instance=$(ours)
        i=$((i + 1))
    done
    # show the Broadway page before GIMP opens its windows
    i=0
    until python3 -c "import socket; socket.create_connection(('127.0.0.1', $CC_BROADWAY_PORT), 1)" \
            2>/dev/null || [ $i -gt 60 ]; do
        sleep 0.5
        i=$((i + 1))
    done
    $cdp "$view" nav:"$page" wait:1500 >/dev/null 2>&1
    touch "$out/page-open"
}

# the dialog: the topmost canvas on the page smaller than the image
# window (Broadway draws each window as a canvas, stacked by z-index)
find_dialog () {
    at=
    i=0
    while [ $i -lt 40 ]; do
        at=$($cdp "$view" nav:"$page" wait:3000 \
          "eval:(() => { const c = [...document.querySelectorAll('canvas')]
            .map(e => [e.getBoundingClientRect(), Number(e.style.zIndex) || 0])
            .filter(([r]) => r.left >= 0 && r.top >= 0 && r.width > 250 && r.height > 250
                             && r.width < 1200)
            .sort((a, b) => b[1] - a[1])[0];
            return c ? [c[0].left, c[0].top, c[0].width, c[0].height].map(Math.round).join(',')
                     : '' })()" 2>/dev/null)
        [ -n "$at" ] && break
        i=$((i + 1))
        sleep 3
    done
    x0=$(echo "$at" | cut -d, -f1)
    y0=$(echo "$at" | cut -d, -f2)
    # (the size, for look.sh)
    # shellcheck disable=SC2034
    dw=$(echo "$at" | cut -d, -f3)
    # shellcheck disable=SC2034
    dh=$(echo "$at" | cut -d, -f4)
}
p () { echo "$(( x0 + $1 )),$(( y0 + $2 ))"; }

wait_for () {
    i=0
    while [ ! -f "$1" ] && [ $i -lt "${2:-60}" ]; do
        sleep 1
        i=$((i + 1))
    done
    [ -f "$1" ]
}
