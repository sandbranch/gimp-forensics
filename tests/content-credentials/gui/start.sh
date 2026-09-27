#!/bin/sh
# Starts the Flatpak GIMP on a Broadway display, on the port
# CC_BROADWAY_PORT (8087 by default; the page is http://127.0.0.1:port/)
# with gui-script.py, which opens CC_GUI_FILE and the Content Credentials
# dialog, in the throwaway profile of tests/content-credentials/run.sh
# (which installs the plug-in there). broadwayd stops when GIMP quits,
# also when GIMP fails. GIMP loads no fonts (--no-fonts): on Broadway it
# often hung at start while loading them. GIMP runs isolated from your own
# folders (tests/isolate.sh), with the throwaway home of run.sh, and as a
# new instance (--new-instance): otherwise it hands the image over to any
# other GIMP of the Flatpak that is running, and quits.
#
#   CC_GUI_FILE=tests/content-credentials/images/c2pa-rs-ocsp.jpg \
#     tests/content-credentials/gui/start.sh
#
# GIMP starts when tests/content-credentials/output/gui/page-open exists:
# GIMP places its windows for the size of the Broadway screen, which is
# that of the page once a browser shows it (common.sh does so first).
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
here=$(cd "$(dirname "$0")" && pwd)
tests=$(dirname "$here")
src=$(dirname "$(dirname "$tests")")
out=$tests/output/gui
profile=$tests/output/profile
port=${CC_BROADWAY_PORT:-8087}
display=$((port - 8080))
mkdir -p "$out"
# no Welcome dialog: GIMP shows it when the profile is of an older
# version, or when asked to
if ! grep -q config-version "$profile/gimprc" 2>/dev/null; then
    version=$(LC_ALL=C flatpak info org.gimp.GIMP | sed -n 's/^ *Version: *//p')
    printf '(config-version "%s")\n' "$version" >> "$profile/gimprc"
fi
grep -q show-welcome-dialog "$profile/gimprc" 2>/dev/null ||
  echo '(show-welcome-dialog no)' >> "$profile/gimprc"
rm -f "$profile/sessionrc"
GIMP_RUN_HOME=${GIMP_RUN_HOME:-$tests/output/gimp-home}
# shellcheck source=SCRIPTDIR/../../isolate.sh
. "$src/tests/isolate.sh"
gimp_run --flatpak --filesystem="$src" \
  --env=GDK_BACKEND=broadway --env=BROADWAY_DISPLAY=:$display \
  --env=GIMP3_DIRECTORY="$profile" --env=CC_OUT="$out" \
  --env=CC_GUI_FILE="${CC_GUI_FILE:?}" -- sh -c \
  "broadwayd --port $port :$display & bw=\$!; trap 'kill \$bw' EXIT; \
   i=0; while [ ! -f '$out/page-open' ] && [ \$i -lt 300 ]; do sleep 0.2; i=\$((i+1)); done; \
   gimp-3.2 --new-instance --no-splash --no-fonts \
   --batch-interpreter python-fu-eval -b \"exec(open('$here/gui-script.py').read())\""
