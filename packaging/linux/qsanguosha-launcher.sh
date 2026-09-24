#!/bin/sh
#
# Portable bundle launcher.
#
# Extract the archive anywhere and run ./QSanguosha - no environment variable
# has to be set by hand.  The launcher only resolves its own directory and hands
# over to the real binary, which finds its private Qt through its
# $ORIGIN-relative RUNPATH plus bin/qt.conf, and its data through the runtime
# layout resolver (bin/../share/qsanguosha).
#
# Deliberately no LD_LIBRARY_PATH/QT_PLUGIN_PATH/QML2_IMPORT_PATH export:
# leaking a private Qt into the environment breaks anything the game launches.
set -eu

HERE="$(dirname "$(readlink -f "${0}")")"
# Qt 6 prefers Wayland when WAYLAND_DISPLAY is set. Hover for QQuickWidget and
# QGraphicsView (and the pointer-effect Tool overlay) then never sees MouseMove.
# If an X11 display is available — including XWayland — use xcb. Opt into
# Wayland with QT_QPA_PLATFORM=wayland.
if [ -z "${QT_QPA_PLATFORM:-}" ] && [ -n "${DISPLAY:-}" ]; then
    export QT_QPA_PLATFORM=xcb
fi
exec "${HERE}/bin/QSanguosha" "$@"
