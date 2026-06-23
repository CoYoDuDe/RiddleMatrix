# RiddleMatrix Windows Manager

Dieses Tool ersetzt im Alltag den gebooteten USB-Stick: Es startet auf dem Windows-Notebook den Hotspot/AP und danach dieselbe lokale Boxenverwaltung, die auch der USB-Stick nutzt.

Der USB-Stick bleibt als Backup erhalten. Die Windows-App schreibt nur dann auf den Stick, wenn `USB-Stick WLAN speichern` genutzt wird.

## Funktionen

- AP-SSID, AP-Passwort, Manager-Port und Box-Subnetz lokal speichern
- AP-SSID und AP-Passwort mit `USB-Stick WLAN speichern` auf die Windows-Partition des bootfaehigen RiddleMatrix-Sticks schreiben
- vor dem AP-Start fragen, ob ein aktuell verbundenes WLAN getrennt werden soll
- Windows Mobile Hotspot per Windows-API starten, damit Windows DHCP/NAT fuer die Boxen bereitstellt
- Fallback auf `netsh wlan hostednetwork`, wenn der Treiber das noch kann
- bei blockiertem Hotspot-Fallback die Windows-Hotspot-Einstellungen oeffnen
- den originalen Flask-Webserver aus `USBStick-Setup/files/usr/local/bin/webserver.py` starten
- die originale USB-Stick-Weboberflaeche aus `USBStick-Setup/files/usr/local/etc/index.html` verwenden
- bekannte Boxen, Reihenfolge, eingebettete Box-Weboberflaechen, Tages-/Trigger-Buchstaben, Farben, Delays und Uebertragung an Boxen wie beim USB-Stick nutzen

## Start

Per Doppelklick:

- `Start-RiddleMatrixWindowsManager.cmd`

Oder direkt in PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File .\Start-RiddleMatrixWindowsManager.ps1
```

Beim ersten Start legt das Tool automatisch eine lokale Python-Umgebung unter `%AppData%\RiddleMatrixWindowsManager\venv` an und installiert `Flask`, `beautifulsoup4` und `requests`.

## Ablauf

1. App starten.
2. SSID und Passwort eintragen.
3. `AP + Manager starten` klicken.
4. Falls das Notebook mit WLAN verbunden ist, die Rueckfrage zum Trennen bestaetigen.
5. Die App startet die originale Manager-Oberflaeche im Browser.

Wenn dein WLAN-Treiber den AP-Start nicht unterstuetzt, oeffne `Windows-Hotspot Einstellungen`, aktiviere den Mobile Hotspot dort und nutze danach `Nur Manager starten`.

`Nur Manager starten` oeffnet den Manager automatisch im Browser. Ein separater Oeffnen-Button ist deshalb nicht noetig.

## Daten

Die Windows-App nutzt lokale Daten unter `%AppData%\RiddleMatrixWindowsManager`:

- `settings.json` fuer Windows-Tool-Einstellungen
- `boxen_config.json` fuer die gleiche Box-Konfiguration, die der USB-Stick unter `/mnt/persist/boxen_config/boxen_config.json` nutzt
- `venv` fuer die lokale Python-Umgebung

Wenn die App vom fertigen RiddleMatrix-USB-Stick gestartet wird, kann sie die Linux-Hotspot-Konfiguration auf der FAT32-Partition unter `config\public_ap.env` aktualisieren. Diese Datei wird beim Booten des Linux-Sticks als `/etc/usbstick/public_ap.env` verwendet.

## Subnetz

Das Standard-Subnetz ist `192.168.137`. Das passt normalerweise zu Windows-Mobile-Hotspot/Internetfreigabe. Der Mobile Hotspot stellt DHCP und NAT bereit, damit die Boxen IP-Adressen bekommen. Wenn Windows auf deinem Notebook ein anderes Hotspot-Netz nutzt, kann das Feld `Box-Subnetz` angepasst werden, zum Beispiel auf `192.168.10`.

## Hinweis zu Windows 11

Einige WLAN-Treiber unterstuetzen `netsh wlan hostednetwork` nicht mehr. Das ist auf aktuellen Windows-10/11-Geraeten normal. Die App nutzt deshalb zuerst den Windows-Mobile-Hotspot per Windows-API. Wenn Windows das blockiert, oeffnet die App die Hotspot-Einstellungen; aktiviere den Hotspot dort manuell und starte danach den Manager.
