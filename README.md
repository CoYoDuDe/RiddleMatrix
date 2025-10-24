# RiddleMatrix

Dieses Projekt steht unter der MIT-Lizenz. Siehe [LICENSE](LICENSE) für Details.
RiddleMatrix ist eine Firmware für den ESP8266, die eine 64x64 RGB-LED-Matrix ansteuert. Für jeden Wochentag und jede der drei RS485-Triggerleitungen lassen sich individuelle Buchstaben, Farben **und Verzögerungszeiten** festlegen. Die Buchstaben erscheinen entweder zeitgesteuert oder per RS485-Trigger. Über WLAN lässt sich das Gerät konfigurieren; alle Einstellungen werden im EEPROM gespeichert.

Siehe [TODO.md](TODO.md) für den Projektfahrplan.

## Hardware-Voraussetzungen

- **ESP8266-Board** (NodeMCU v2 empfohlen)
- **64x64-RGB-LED-Matrix** mit FM6126A-Treiber (1/32-Scan)
- **DS1307-RTC-Modul**
- **RS485-Transceiver** für externe Trigger
- Verdrahtung gemäß `config.h`

## WLAN-Konfiguration

1. In `config.h` `wifi_ssid`, `wifi_password`, `hostname` und optional `wifi_connect_timeout` anpassen.
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
Dateiangabe für eine vollständige Suite.

## Weitere Schritte

- LED-Matrix gemäß `config.h` anschließen.
- RTC an `I2C_SDA` und `I2C_SCL` anschließen.
- RS485-Enable-Pin an `GPIO_RS485_ENABLE` verbinden.
- Serielle Konsole bei 19200 Baud für Debug-Ausgaben prüfen.

Nach der Einrichtung zeigt die Firmware die Buchstaben automatisch an und kann über die Weboberfläche gesteuert werden.

## Konfiguration

`config.h` enthält Platzhalter-WLAN-Daten, falls noch nichts im EEPROM gespeichert ist. Echte Zugangsdaten sollten **nicht** ins Repository gelangen. Sie können initial über das EEPROM oder die Konfigurationsseite gesetzt werden. Der Parameter `wifi_connect_timeout` bestimmt, wie lange die Verbindung versucht wird (Standard 30 Sekunden).

> **Hinweis:** Die Firmware erkennt jetzt gelöschte EEPROM-Zellen plattformunabhängig. Vergleiche gegen `0xFF` erfolgen explizit auf `uint8_t`-Basis, sodass Host-Tests und der ESP8266 dieselbe Initialisierung der WLAN-Defaults auslösen.

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

Legacy-Skripte wurden in [`USBStick-Setup/archive/legacy-root-scripts/`](USBStick-Setup/archive/legacy-root-scripts) abgelegt und stehen weiterhin als Referenz zur Verfügung.

### Geschützter Shutdown-Endpunkt

Die Weboberfläche des USB-Stick-Setups löst das Herunterfahren des Geräts über den Endpunkt `/shutdown` aus.
Damit nur berechtigte Clients diesen Vorgang starten können, gelten seitdem folgende Regeln:

- Lokale Zugriffe vom Gerät selbst (`127.0.0.1` oder `::1`) bleiben ohne weitere Maßnahmen möglich.
- Für Zugriffe aus anderen Netzen muss ein Token `SHUTDOWN_TOKEN` hinterlegt werden – idealerweise in
  `/etc/usbstick/public_ap.env` oder als Environment-Variable. Die Weboberfläche fragt das Token beim ersten Klick
  auf „Herunterfahren“ ab, speichert es im Browser und übermittelt es anschließend per HTTP-Header `X-Api-Key`.
- Ungültige oder fehlende Tokens führen zu HTTP 403. Der Browser blendet in diesem Fall einen Hinweis ein und verlangt
  bei Bedarf die erneute Eingabe.

Vor jedem Abschalten erscheint zusätzlich ein Bestätigungsdialog, damit unbeabsichtigte Klicks keine sofortige
Abschaltung mehr auslösen. Das Frontend informiert außerdem darüber, dass der Shutdown einige Minuten dauern kann.

### Geschützter Reload-All-Endpunkt

Auch der Verwaltungsendpunkt `/reload_all`, der alle bekannten Boxen aus der Konfigurationsdatei löscht und über
`dnsmasq.leases` neu erlernt, ist jetzt gegen unbefugte Zugriffe gesichert. Die Regeln entsprechen dem
Shutdown-Endpoint:

- Lokale Zugriffe (`127.0.0.1` bzw. `::1`) bleiben ohne weiteres Token erlaubt.
- Für entfernte Clients wird derselbe Header `X-Api-Key` erwartet. Der Schlüssel wird weiterhin über die Variable
  `SHUTDOWN_TOKEN` (z. B. in `/etc/usbstick/public_ap.env`) bereitgestellt, damit keine zusätzliche Geheimnisverwaltung
  notwendig ist.
- Die Weboberfläche blendet den Button „🔄 Boxen neu lernen“ aus, solange kein gültiges Token hinterlegt wurde, und
  bietet einen separaten Dialog zum Hinterlegen des Tokens an. Vor dem Neu-Laden der Boxen erscheint zusätzlich eine
  Sicherheitsabfrage.
- Fehlgeschlagene Versuche führen zu HTTP 403, werden serverseitig protokolliert und löschen das gespeicherte Token im
  Browser, damit Anwender:innen sofort eine neue Eingabe erzwingen können.

Der Workflow bleibt damit kompatibel zur bestehenden Shutdown-Logik und nutzt dieselbe Konfiguration.
