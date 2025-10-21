# RiddleMatrix

Dieses Projekt steht unter der MIT-Lizenz. Siehe [LICENSE](LICENSE) für Details.
RiddleMatrix ist eine Firmware für den ESP8266, die eine 64x64 RGB-LED-Matrix ansteuert. Für jeden Wochentag kann ein Buchstabe festgelegt werden. Die Buchstaben erscheinen entweder zeitgesteuert oder per RS485-Trigger. Über WLAN lässt sich das Gerät konfigurieren; alle Einstellungen werden im EEPROM gespeichert.

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

## Weitere Schritte

- LED-Matrix gemäß `config.h` anschließen.
- RTC an `I2C_SDA` und `I2C_SCL` anschließen.
- RS485-Enable-Pin an `GPIO_RS485_ENABLE` verbinden.
- Serielle Konsole bei 19200 Baud für Debug-Ausgaben prüfen.

Nach der Einrichtung zeigt die Firmware die Buchstaben automatisch an und kann über die Weboberfläche gesteuert werden.

## Konfiguration

`config.h` enthält Platzhalter-WLAN-Daten, falls noch nichts im EEPROM gespeichert ist. Echte Zugangsdaten sollten **nicht** ins Repository gelangen. Sie können initial über das EEPROM oder die Konfigurationsseite gesetzt werden. Der Parameter `wifi_connect_timeout` bestimmt, wie lange die Verbindung versucht wird (Standard 30 Sekunden).

## USB-Stick-Setup für das Boxen-Ökosystem

Im Verzeichnis [`USBStick-Setup/`](USBStick-Setup) befindet sich ein portabler Installer, mit dem vorbereitete Dateien auf ein Venus-OS- oder Debian-Zielsystem kopiert werden. Der neue Einstiegspunkt [`USBStick-Setup/setup.sh`](USBStick-Setup/setup.sh) übernimmt sämtliche Kopier- und Nacharbeiten, setzt korrekte Dateirechte und aktiviert die benötigten Systemd-Units.

### Schnellstart

```bash
cd USBStick-Setup
sudo ./setup.sh
```

Der Installer kopiert standardmäßig den Inhalt von `USBStick-Setup/files/` auf das laufende System (`/`). Mit `--target` kann ein anderes Root-Verzeichnis (z. B. ein gemountetes Venus-OS-Image) angegeben werden, `--dry-run` zeigt geplante Schritte ohne Änderungen an und `--skip-systemd`/`--skip-hooks` deaktivieren optionale Aktionen. Weitere Details finden sich in der README im Unterordner [`USBStick-Setup`](USBStick-Setup).

Legacy-Skripte wurden in [`USBStick-Setup/archive/legacy-root-scripts/`](USBStick-Setup/archive/legacy-root-scripts) abgelegt und stehen weiterhin als Referenz zur Verfügung.
