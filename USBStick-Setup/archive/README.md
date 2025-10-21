# Archivierte Setup-Skripte

Die Shell-Skripte in diesem Ordner wurden durch den neuen Installer [`../setup.sh`](../setup.sh) ersetzt. Sie verbleiben hier
nur zur Dokumentation bisheriger Workflows und sollten nicht mehr direkt auf produktiven Systemen ausgeführt werden.

| Datei | Beschreibung |
| --- | --- |
| `debian_minimal_setup.sh` | Frühere Minimalinstallation mit Paket- und Servicekonfiguration. |
| `debian_setup.sh` | Ausführlichere Variante mit zusätzlichen Paketen und Konfigurationen. |
| `setup.sh` | Wrapper, der `debian_minimal_setup.sh` startete. |
| `ssetup.sh` | Legacy-Skript mit eingebettetem Webserver- und UI-Rollout. |

Die Skripte werden bewusst nicht mehr in `files/` ausgeliefert, damit sie nicht versehentlich auf Zielsysteme kopiert werden.
