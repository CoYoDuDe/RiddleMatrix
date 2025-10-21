#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALLER="$SCRIPT_DIR/install_public_ap.sh"

if [[ ! -x "$INSTALLER" ]]; then
    echo "❌ Erwartetes Installer-Skript $INSTALLER fehlt oder ist nicht ausführbar." >&2
    exit 1
fi

# SetupHelper ruft Skripte häufig ohne TTY auf – sorge für klare Logausgaben.
echo "▶️ Starte Hotspot-Installer für SetupHelper..."
"$INSTALLER" "$@"
echo "✅ SetupHelper-Hotspot-Installer abgeschlossen."
