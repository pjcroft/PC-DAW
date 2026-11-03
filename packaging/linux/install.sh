#!/bin/sh
# Installs Croft DAW for the current user: binary, launcher entry and icon.
# Run from the folder containing "Croft DAW", croft-daw.desktop and icon.png.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
BIN="$HOME/.local/bin"
APPS="$HOME/.local/share/applications"
ICONS="$HOME/.local/share/icons/hicolor/512x512/apps"
mkdir -p "$BIN" "$APPS" "$ICONS"
install -m 755 "$HERE/Croft DAW" "$BIN/Croft DAW"
install -m 644 "$HERE/icon.png" "$ICONS/croft-daw.png"
sed "s|^Exec=.*|Exec=\"$BIN/Croft DAW\" %F|" "$HERE/croft-daw.desktop" > "$APPS/croft-daw.desktop"
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APPS" || true
echo "Installed. Croft DAW is now in your applications menu."
