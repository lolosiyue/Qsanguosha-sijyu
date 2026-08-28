#!/bin/sh
# Portable bundle launcher for the dedicated server (see qsanguosha-launcher.sh).
set -eu

HERE="$(dirname "$(readlink -f "${0}")")"
exec "${HERE}/bin/qsanguosha_server" "$@"
