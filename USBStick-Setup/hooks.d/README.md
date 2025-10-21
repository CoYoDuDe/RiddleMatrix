# Post-Install Hooks

Alle ausführbaren `.sh`-Dateien in diesem Ordner werden nach erfolgreichem Kopieren der Dateien durch [`../setup.sh`](../setup.sh)
in alphabetischer Reihenfolge aufgerufen. Die Skripte erhalten das Ziel-Root als erstes Argument und können über die Umgebungsvariable
`TARGET_ROOT` darauf zugreifen. Nicht ausführbare Dateien werden ignoriert.

Nutzen Sie diesen Mechanismus beispielsweise für Paketinstallationen (`apt`, `opkg`), Validierungen oder das Schreiben zusätzlicher
Konfigurationsdateien, ohne den Kerninstaller anpassen zu müssen.
