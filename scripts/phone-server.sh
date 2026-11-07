#!/bin/sh
# Сервер для телефона без окна: launchd запускает morph --server при входе в
# систему и перезапускает, если тот упал. caffeinate -s не даёт маку уснуть,
# пока он на зарядке, иначе телефон дома до него не достучится. На батарее
# мак засыпает как обычно
set -e
LABEL=io.github.don-qu1xote.morph.server
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
HERE="$(cd "$(dirname "$0")" && pwd)"
DOMAIN="gui/$(id -u)"

case "${1:-}" in
install)
    APP="${2:-}"
    if [ -z "$APP" ]; then
        if [ -d /Applications/morph.app ]; then APP=/Applications/morph.app; else APP="$HERE/../build/morph.app"; fi
    fi
    BIN="$(cd "$APP" 2>/dev/null && pwd)/Contents/MacOS/morph"
    if [ ! -x "$BIN" ]; then
        echo "morph not found at $APP. Usage: $0 install /path/to/morph.app"
        exit 1
    fi
    mkdir -p "$HOME/Library/LaunchAgents"
    sed "s|@MORPH@|$BIN|" "$HERE/../macos/morph-server.plist" > "$PLIST"
    launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null || true
    launchctl bootstrap "$DOMAIN" "$PLIST"
    echo "Phone server now starts with your Mac: $BIN --server"
    echo "Log: /tmp/morph-server.log"
    ;;
uninstall)
    launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null || true
    rm -f "$PLIST"
    echo "Phone server no longer starts on its own"
    ;;
status)
    if launchctl print "$DOMAIN/$LABEL" >/dev/null 2>&1; then
        launchctl print "$DOMAIN/$LABEL" | grep -E "^\s*(state|pid) =" || true
    else
        echo "Not installed"
    fi
    [ -f /tmp/morph-server.log ] && tail -n 5 /tmp/morph-server.log
    ;;
*)
    echo "Usage: $0 install [/path/to/morph.app] | uninstall | status"
    exit 1
    ;;
esac
