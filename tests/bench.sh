#!/bin/sh
# How long the operations take on a 24 megapixel image (tests/bench.c),
# with the build in build/ (or $BUILD), inside the Flatpak SDK with the
# Flatpak GIMP or natively with GIMP_FLATPAK=0, isolated from your
# folders (GIMP_RUN_HOME, tests/output/gimp-home: see gimp-build.sh).
#
#   tests/bench.sh                       every operation, defaults, 6000 x 4000
#   tests/bench.sh --size 3000x2000 --threads 1 \
#       --op forensics:error-level quality=95 chroma=4:4:4
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
set -e
here=$(cd "$(dirname "$0")" && pwd)
top=$(dirname "$here")
build=${BUILD:-build}
[ -x "$top/$build/tests/bench" ] || { echo "no $build/tests/bench: build first (README)" >&2; exit 2; }
GIMP_RUN_HOME=${GIMP_RUN_HOME:-$here/output/gimp-home}
export GIMP_RUN_HOME
set -- "$build"/*.so "$@"
gimp_build=${GIMP_BUILD:-$top/../gimp-devtools/gimp-build.sh}
if [ "${GIMP_FLATPAK:-1}" = 1 ] && command -v flatpak >/dev/null 2>&1 &&
   flatpak info org.gimp.GIMP >/dev/null 2>&1; then
  cd "$top" && exec "$gimp_build" "$top" "$build/tests/bench" "$@"
fi
cd "$top" && exec "$build/tests/bench" "$@"
