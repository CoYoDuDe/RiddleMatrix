# USBStick-Setup

Dieses Verzeichnis enthält alle Ressourcen, die auf einen USB-Stick kopiert werden können, um ein Venus-OS- oder Debian-basiertes
System für den Märchen-Manager vorzubereiten. Die Struktur unter `files/` entspricht dem Root-Dateisystem des Zielgerätes.

Der zentrale Einstiegspunkt ist [`setup.sh`](setup.sh). Das Skript kopiert den vorbereiteten Dateibaum, setzt Dateirechte und
aktiviert optional die bereitgestellten Systemd-Units. Darüber hinaus können optionale Hooks ausgeführt werden, um zusätzliche
Aufgaben – etwa Paketinstallationen oder Firmware-Checks – einzubinden.

## Voraussetzungen

- Bash 5 oder neuer
- `rsync` (empfohlen, fällt bei Nichtverfügbarkeit automatisch auf `tar` zurück)
- Root-Rechte, sofern direkt auf `/` geschrieben wird

## Aufruf

```bash
sudo ./setup.sh               # Installation auf das laufende System
sudo ./setup.sh --dry-run     # Nur anzeigen, was passieren würde
sudo ./setup.sh --target /mnt/venus-os   # Installation in ein gemountetes Root-Dateisystem
```

### Optionen

| Option | Beschreibung |
| --- | --- |
| `--target <pfad>` | Ziel-Root (muss absolut sein, Standard `/`). |
| `--dry-run` | Zeigt geplante Schritte ohne Änderungen. |
| `--skip-systemd` | Überspringt `systemctl daemon-reload` und `systemctl enable`. |
| `--skip-hooks` | Führt keine Skripte aus `hooks.d/` aus. |

## Hooks

Individuelle Nacharbeiten lassen sich als ausführbare Shell-Skripte (Dateiendung `.sh`) in `hooks.d/` ablegen. Die Skripte
werden in alphabetischer Reihenfolge mit dem Ziel-Root als einzigem Argument aufgerufen und erben die Umgebungsvariable
`TARGET_ROOT`.

## Legacy-Skripte

Die vorherigen monolithischen Installationsskripte wurden in [`archive/legacy-root-scripts/`](archive/legacy-root-scripts)
archiviert. Sie dienen als Referenz und werden nicht mehr automatisch ausgeführt.
