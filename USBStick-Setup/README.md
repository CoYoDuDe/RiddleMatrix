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
- Installiertes `dnsmasq`-Paket (stellt die Gruppe `dnsmasq` bereit, damit die Lease-Datei beschreibbar bleibt)

## Aufruf

```bash
sudo ./setup.sh               # Installation auf das laufende System
sudo ./setup.sh --dry-run     # Nur anzeigen, was passieren würde
sudo ./setup.sh --target /mnt/venus-os   # Installation in ein gemountetes Root-Dateisystem
```

## Nach der Installation

Der Installer ruft auf Live-Systemen automatisch `systemctl daemon-reload` auf und startet die bereitgestellten Units neu. Bei
Offline-Installationen (z. B. `--target /mnt/...`) oder wenn `--skip-systemd` verwendet wurde, sollten diese Schritte nach dem
Kopieren manuell erfolgen:

```bash
sudo systemctl daemon-reload
sudo systemctl enable kiosk-startx.service
sudo systemctl restart kiosk-startx.service
sudo systemctl status kiosk-startx.service
```

Die Datei [`files/home/kioskuser/.xinitrc`](files/home/kioskuser/.xinitrc) sorgt dafür, dass `startx` direkt das Skript
[`start_firefox.sh`](files/home/kioskuser/start_firefox.sh) ausführt. So wird der Firefox-Kiosk nach einem Neustart zuverlässig
im Vollbild gestartet.

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

### Automatische Webserver-Provisionierung

Der Hook [`hooks.d/10-provision-webserver.sh`](hooks.d/10-provision-webserver.sh) kümmert sich nach dem Kopieren der Dateien
automatisiert um alle Laufzeitabhängigkeiten des Märchen-Managers:

- prüft mittels `dpkg-query`, ob benötigte Debian-Pakete wie `dnsmasq`, `hostapd`, `rfkill`, `x11-xserver-utils`, `python3`
  (inkl. `python3-venv`) und optionale Firmware-Pakete installiert sind und stößt bei Bedarf ein `apt-get install` an
- erzeugt das virtuelle Python-Umfeld unter `/usr/local/venv/maerchen` mit `python3 -m venv`, falls es noch nicht existiert
- installiert fehlende Python-Abhängigkeiten (`flask`, `requests`, `beautifulsoup4`) idempotent via `pip`

Die Provisionierung läuft sowohl bei einer Installation ins aktive Root-Dateisystem (`TARGET_ROOT=/`) als auch bei einem
gemounteten Ziel (sofern `chroot` verfügbar ist). Dadurch müssen Operator:innen die Abhängigkeiten nicht mehr manuell
nachinstallieren – der Webserver ist nach Abschluss von `setup.sh` sofort startklar.

## Hotspot-Zugangsdaten anpassen

Der USB-Stick liefert mit [`files/etc/usbstick/public_ap.env.example`](files/etc/usbstick/public_ap.env.example) eine Vorlage
für die WLAN-Zugangsdaten des Boot-Hotspots. Während der Installation kopiert `setup.sh` die Vorlage nach
`/etc/usbstick/public_ap.env`, sofern dort noch keine individuelle Datei existiert oder lediglich die unveränderte Vorlage
vorliegt. Die Datei wird mit restriktiven Rechten (`0640`) abgelegt.

Passen Sie unmittelbar nach der Installation SSID und WPA-Passphrase an, damit der Hotspot nicht mit den Standardwerten
(`RiddleMatrix-Hotspot` / `BittePasswortAnpassen123!`) aktiv bleibt:

```bash
sudo editor /etc/usbstick/public_ap.env
# Beispielinhalt
SSID="MeinIndividuellesNetz"
WPA_PASSPHRASE="SehrSicheresKennwort123"
```

`bootlocal.sh` und `root/install_public_ap.sh` starten den Hotspot weiterhin, selbst wenn die Datei fehlt oder leer ist: Sie
protokollieren den Grund und greifen auf dieselben Standardwerte zurück. Sobald gültige Werte eingetragen wurden, greifen die
Skripte automatisch auf die konfigurierte SSID/Passphrase zurück und geben im Fehlerfall eine gut verständliche Meldung aus.

## Gesicherte Lease-Datei für dnsmasq

`setup.sh` setzt beim Abschluss der Installation die Datei `var/lib/misc/dnsmasq.leases` auf den Eigentümer `root:dnsmasq`
und den Modus `0640`. Fehlt die Datei, wird sie mit exakt diesen Rechten idempotent angelegt, sodass nachfolgende Läufe keine
breiteren Berechtigungen vergeben. Sollte die Gruppe `dnsmasq` (bereitgestellt durch das gleichnamige Paket) noch nicht
existieren, weist das Skript darauf hin. Sobald der Dienst installiert ist, kann `dnsmasq` trotz restriktiver Zugriffsrechte
problemlos starten und die Lease-Datei exklusiv schreiben, während andere Benutzer:innen nur noch lesenden Zugriff über die
Gruppenmitgliedschaft erhalten.

## Legacy-Skripte

Die vorherigen monolithischen Installationsskripte wurden in [`archive/legacy-root-scripts/`](archive/legacy-root-scripts)
archiviert. Sie dienen als Referenz und werden nicht mehr automatisch ausgeführt.
