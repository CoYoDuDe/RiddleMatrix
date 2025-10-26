# RiddleMatrix

Dieses Projekt steht unter der MIT-Lizenz. Siehe [LICENSE](LICENSE) für Details.
RiddleMatrix ist eine Firmware für den ESP8266, die eine 64x64 RGB-LED-Matrix ansteuert. Für jeden Wochentag und jede der drei RS485-Triggerleitungen lassen sich individuelle Buchstaben, Farben **und Verzögerungszeiten** festlegen. Die Buchstaben erscheinen entweder zeitgesteuert oder per RS485-Trigger. Die Konfiguration erfolgt über die integrierte Weboberfläche; alle Einstellungen werden im EEPROM gespeichert.

Siehe [TODO.md](TODO.md) für den Projektfahrplan.

## Hardware-Voraussetzungen

- **ESP8266-Board** (NodeMCU v2 empfohlen)
- **64x64-RGB-LED-Matrix** mit FM6126A-Treiber (1/32-Scan)
- **DS1307-RTC-Modul**
- **RS485-Transceiver** für externe Trigger
- Verdrahtung gemäß `config.h`

## Konfigurationsoberfläche

1. In `config.h` die Variablen `wifi_ssid`, `wifi_password`, `hostname` und optional `wifi_connect_timeout` anpassen.
2. Firmware kompilieren und hochladen.
3. Nach erfolgreicher Verbindung `http://<hostname>` aufrufen und die Zugangsdaten im EEPROM speichern.

## Kompilieren und Hochladen

### Arduino IDE

1. Bibliotheken installieren: **PxMatrix**, **ESPAsyncWebServer**, **ArduinoJson**, **RTClib** und **Ticker** (bereits enthalten).
2. Unter *Werkzeuge → Board* **NodeMCU 1.0 (ESP-12E Module)** auswählen.
3. `Firmware.ino` öffnen, prüfen und hochladen.

### PlatformIO

PlatformIO lässt sich über `pip` installieren. Für diese Anleitung wurde Version
**6.1.18** verwendet:

```bash
pip install platformio==6.1.18
```

Nach der Installation muss der Befehl `pio` im `PATH` verfügbar sein.

1. `platformio.ini` und die Quellen in `src/` sind bereits vorkonfiguriert. Sämtliche
   Abhängigkeiten werden beim ersten Build automatisch aus dieser Datei
   installiert.
2. Die Bibliothek **PxMatrix** installieren:
   - Online: `pio lib install 2dom/PxMatrix`
   - Offline: das [PxMatrix-Repository](https://github.com/2dom/PxMatrix) klonen und in `lib/` ablegen.
3. Firmware kompilieren:
   ```bash
   pio run
   ```
4. Firmware auf das Board flashen:
   ```bash
   pio run -t upload
   ```

## Tests

Vor den Host-Tests müssen die Python-Abhängigkeiten installiert werden. Die Datei
`requirements-test.txt` bündelt aktuell die Flask-Version, die für die Webserver-Tests
benötigt wird:

```bash
pip install -r requirements-test.txt
pytest tests/test_webserver.py
```

Die Tests lassen sich beliebig kombinieren, zum Beispiel mit `pytest` ohne
Dateiangabe für eine vollständige Suite. Für den USB-Stick-Installer steht
zusätzlich ein Dry-Run-Test zur Verfügung, der beide Pfade (`TARGET_ROOT=/`
und ein gemountetes Zielsystem) abdeckt und sicherstellt, dass die Provisionierung
ihre Arbeitsschritte sauber protokolliert, ohne Änderungen vorzunehmen:

```bash
pytest tests/test_provision_hook.py
```

## Weitere Schritte

- LED-Matrix gemäß `config.h` anschließen.
- RTC an `I2C_SDA` und `I2C_SCL` anschließen.
- RS485-Enable-Pin an `GPIO_RS485_ENABLE` verbinden.
- Serielle Konsole bei 19200 Baud für Debug-Ausgaben prüfen.

Nach der Einrichtung zeigt die Firmware die Buchstaben automatisch an und kann über die Weboberfläche gesteuert werden.

## Konfiguration

`config.h` enthält Platzhalter für drahtlose Zugangsdaten, falls noch nichts im EEPROM gespeichert ist. Echte Zugangsdaten sollten **nicht** ins Repository gelangen. Sie können initial über das EEPROM oder die Konfigurationsseite gesetzt werden. Der Parameter `wifi_connect_timeout` bestimmt, wie lange die Verbindung versucht wird (Standard 30 Sekunden).

> **Hinweis:** Die Firmware erkennt jetzt gelöschte EEPROM-Zellen plattformunabhängig. Vergleiche gegen `0xFF` erfolgen explizit auf `uint8_t`-Basis, sodass Host-Tests und der ESP8266 dieselbe Initialisierung der Voreinstellungen auslösen.

### Mehrspuriges Buchstabenraster

Die Tagesbuchstaben werden jetzt dreidimensional abgelegt:

- `dailyLetters[trigger][tag]` speichert den Buchstaben pro Triggerleitung und Wochentag.
- `dailyLetterColors[trigger][tag]` enthält die passende Farbe als `#RRGGBB`-String.

Trigger-Index `0` entspricht RS485-Trigger 1, Index `1` Trigger 2 usw. Die Weboberfläche unter `/` zeigt die Werte als Matrix an und erlaubt das gleichzeitige Aktualisieren über `/updateAllLetters`.

| Wochentag   | Trigger 1 (`#RRGGBB`) | Trigger 2 (`#RRGGBB`) | Trigger 3 (`#RRGGBB`) |
|-------------|-----------------------|-----------------------|-----------------------|
| Sonntag     | A (`#FF0000`)         | H (`#FFFFFF`)         | O (`#FFA07A`)         |
| Montag      | B (`#00FF00`)         | I (`#FFD700`)         | P (`#20B2AA`)         |
| Dienstag    | C (`#0000FF`)         | J (`#ADFF2F`)         | Q (`#87CEFA`)         |
| Mittwoch    | D (`#FFFF00`)         | K (`#00CED1`)         | R (`#FFE4B5`)         |
| Donnerstag  | E (`#FF00FF`)         | L (`#9400D3`)         | S (`#DA70D6`)         |
| Freitag     | F (`#00FFFF`)         | M (`#FF69B4`)         | T (`#90EE90`)         |
| Samstag     | G (`#FFA500`)         | N (`#1E90FF`)         | U (`#FFDAB9`)         |

Die HTTP-Endpunkte `/displayLetter` und `/triggerLetter` akzeptieren optional den Parameter `trigger=<1-3>` für Tests je Leitung. Wird kein Trigger angegeben, nutzt die Firmware standardmäßig Leitung 1. Ältere EEPROM-Daten mit eindimensionalen Tagesbuchstaben werden beim ersten Start automatisch migriert.

### Zusatzglyphen

Neben den Großbuchstaben stehen mehrere vordefinierte Symbole zur Verfügung. `'#'` rendert die Sonne, `'~'` zeigt ein Funksignal, `'&'` das Riesenrad und `'?'` den Riddler. Neu hinzugekommen ist `'*'` für das kombinierte „Sun+Rad“-Glyph, das Sonne und Riesenrad zu einem 32×32-Pixelmotiv verschmilzt.

### Verzögerungsmatrix pro Trigger & Tag

- `letter_trigger_delays[trigger][tag]` verwaltet die Wartezeit (Sekunden) vor der Anzeige.
- Die Weboberfläche stellt die Werte als Tabelle dar und validiert Eingaben auf ganzzahlige Werte zwischen 0 und 999.
- Die API `/updateTriggerDelays` akzeptiert ein `FormData`-Payload mit Feldern `delay_<triggerIndex>_<dayIndex>` (Indexbeginn 0). Erfolgreiche Aufrufe speichern Matrix, Buchstaben, Farben und sonstige Parameter gemeinsam im EEPROM (`saveConfig()`).
- Legacy-Konfigurationen mit drei Verzögerungswerten werden beim Laden gleichmäßig auf alle Wochentage verteilt.
- Die API `/api/trigger-delays` stellt die aktuelle Matrix als JSON mit numerischen Werten bereit. Die Schlüssel folgen den Kürzeln
  `{"so", "mo", "di", "mi", "do", "fr", "sa"}` und jede Liste enthält die Verzögerungen der drei Trigger (Sekunden):

  ```json
  {
    "delays": {
      "mo": [0, 10, 0],
      "di": [5, 15, 0]
    }
  }
  ```

  SetupHelper nutzt diesen Endpunkt, um `_normalize_delay_list()` unverändert auf rohe Zahlenwerte anzuwenden.

### Anzeigeeinstellungen & REST-API `/updateDisplaySettings`

- **`brightness`** (`1`–`255`): Helligkeit der Matrix. Werte außerhalb führen zu HTTP 400.
- **`letter_time`** (`1`–`60` Sekunden): Dauer pro Buchstabe. Nur ganzzahlige Sekunden werden akzeptiert.
- **`auto_interval`** (`30`–`600` Sekunden): Intervall für den Automodus.
- **`auto_mode`** (optional): `on`, `off`, `true`, `false`, `1` oder `0`. Nicht angegebene Felder deaktivieren den Automodus.

Die Weboberfläche weist auf diese Grenzen hin. Der Handler prüft jede Eingabe strikt (Parsing als `long`/`unsigned long`) und beantwortet Verstöße mit HTTP 400 inklusive deutscher Fehlermeldung.

## USB-Stick-Setup für das Boxen-Ökosystem

Im Verzeichnis [`USBStick-Setup/`](USBStick-Setup) befindet sich ein portabler Installer, mit dem vorbereitete Dateien auf ein Venus-OS- oder Debian-Zielsystem kopiert werden. Der neue Einstiegspunkt [`USBStick-Setup/setup.sh`](USBStick-Setup/setup.sh) übernimmt sämtliche Kopier- und Nacharbeiten, setzt korrekte Dateirechte und aktiviert die benötigten Systemd-Units.

### Schnellstart

```bash
cd USBStick-Setup
sudo ./setup.sh
```

Der Installer kopiert standardmäßig den Inhalt von `USBStick-Setup/files/` auf das laufende System (`/`). Mit `--target` kann ein anderes Root-Verzeichnis (z. B. ein gemountetes Venus-OS-Image) angegeben werden, `--dry-run` zeigt geplante Schritte ohne Änderungen an und `--skip-systemd`/`--skip-hooks` deaktivieren optionale Aktionen. Weitere Details finden sich in der README im Unterordner [`USBStick-Setup`](USBStick-Setup).

> **Hinweis:** Der Provisionierungs-Hook `hooks.d/10-provision-webserver.sh` prüft automatisch, ob er auf dem Live-System arbeitet oder ein Ziel unterhalb von `--target` versorgt. Für Offline-Installationen wird das virtuelle Python-Umfeld via `chroot` mit dem Python des Zielsystems erstellt, sodass erzeugte Wheels und Binaries zur Zielarchitektur passen. Ist `PROVISION_DRY_RUN=1` gesetzt (zum Beispiel in Tests), werden alle Schritte nur protokolliert.

> **Abhängigkeiten:** Damit der Webserver verbundene Geräte zuverlässig erkennen kann, muss das Paket `iputils-ping` installiert sein. Der Provisionierungs-Hook fügt es automatisch zur Paketliste hinzu, manuelle Installationen sollten es explizit berücksichtigen (`sudo apt install iputils-ping`).

Legacy-Skripte wurden in [`USBStick-Setup/archive/legacy-root-scripts/`](USBStick-Setup/archive/legacy-root-scripts) abgelegt und stehen weiterhin als Referenz zur Verfügung.

### Hotspot-Zugangsdaten für den USB-Stick-Installer

- Die Vorlage [`USBStick-Setup/files/etc/usbstick/public_ap.env.example`](USBStick-Setup/files/etc/usbstick/public_ap.env.example) enthält gültige Platzhalterwerte (`RiddleMatrix-Hotspot`, `BittePasswortAnpassen123!`) für SSID und WPA-Passphrase.
- `setup.sh` kopiert die Vorlage bei der Installation einmalig nach `/etc/usbstick/public_ap.env`, sofern dort noch keine Datei existiert. Dry-Runs weisen den Schritt explizit als geplante Aktion aus. Sowohl der reguläre `rsync`-Pfad als auch der `tar`-Fallback schließen die Datei beim reinen Kopieren des Payloads aus und protokollieren, dass vorhandene Hotspot-Konfigurationen unangetastet bleiben.
- `bootlocal.sh` und `root/install_public_ap.sh` protokollieren fehlende oder unvollständige Dateien, verweisen auf die Vorlage und greifen automatisch auf dieselben Standardwerte zurück, anstatt mit einem Fehler abzubrechen.
- Nach der Installation sollten die Werte in `/etc/usbstick/public_ap.env` direkt angepasst werden, um individuelle Hotspot-Zugangsdaten zu verwenden.

### Absicherung des Shutdown-Endpunkts der Weboberfläche

- Der lokale Flask-Webserver stellt den Poweroff-Endpunkt `/shutdown` ausschließlich für berechtigte Clients bereit. Loopback-Anfragen (`127.0.0.1` beziehungsweise `::1`) sind weiterhin ohne zusätzliche Angaben möglich.
- Für entfernte Verbindungen muss der HTTP-Header `X-Setup-Token` gesetzt werden. Der gültige Token wird über die Umgebungsvariable `SHUTDOWN_TOKEN` oder die Datei `/etc/usbstick/public_ap.env` verteilt.
- Fehlende oder ungültige Tokens führen zu HTTP-Status 403 und werden im Log des Webservers als Warnung dokumentiert. Auf diese Weise bleiben versehentliche oder unautorisierte Shutdown-Versuche nachvollziehbar.
- Tests für diesen Pfad befinden sich in [`tests/test_webserver.py`](tests/test_webserver.py) und decken erfolgreiche sowie abgelehnte Anfragen ab.

