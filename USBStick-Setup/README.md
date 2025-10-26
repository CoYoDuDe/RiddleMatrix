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

## Firefox-Kiosk sicherstellen

Der Dienst [`kiosk-startx.service`](files/etc/systemd/system/kiosk-startx.service) startet `startx` nun explizit mit dem
Firefox-Wrapper [`start_firefox.sh`](files/home/kioskuser/start_firefox.sh). Zusätzlich liegt im Benutzerverzeichnis des
Kiosk-Accounts eine [`~/.xinitrc`](files/home/kioskuser/.xinitrc), die denselben Wrapper via `exec` aufruft. Dadurch
startet der Firefox-Kiosk zuverlässig, selbst wenn `startx` ohne Argumente ausgeführt wird.

Nach einer Installation auf dem laufenden System sorgt `setup.sh` automatisch für `systemctl daemon-reload`, aktiviert die
Units und stößt einen Neustart des Kiosk-Dienstes an. Falls die Installation jedoch mit `--skip-systemd` oder in ein
gemountetes Root-Dateisystem erfolgt, sollten folgende Schritte manuell nachgeholt werden:

```bash
sudo systemctl daemon-reload
sudo systemctl enable kiosk-startx.service
sudo systemctl restart kiosk-startx.service
sudo systemctl status kiosk-startx.service
```

Die Statusabfrage stellt sicher, dass `startx` sauber hochfährt und Firefox im Kiosk-Modus läuft. Treten Fehler auf,
liefern die Journal-Einträge (`journalctl -u kiosk-startx.service`) Hinweise zur Diagnose.

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
