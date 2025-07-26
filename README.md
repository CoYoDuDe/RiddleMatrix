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

`platformio` mit `pip install platformio` installieren und sicherstellen, dass der Befehl im `PATH` liegt.

1. `platformio.ini` und die Quellen in `src/` sind bereits vorkonfiguriert.
2. Die Bibliothek **PxMatrix** installieren:
   - Online: `pio lib install 2dom/PxMatrix`
   - Offline: das [PxMatrix-Repository](https://github.com/2dom/PxMatrix) klonen und in `lib/` ablegen.
3. Mit `pio run` kompilieren.
4. Mit `pio run -t upload` auf das Board flashen.

## Weitere Schritte

- LED-Matrix gemäß `config.h` anschließen.
- RTC an `I2C_SDA` und `I2C_SCL` anschließen.
- RS485-Enable-Pin an `GPIO_RS485_ENABLE` verbinden.
- Serielle Konsole bei 19200 Baud für Debug-Ausgaben prüfen.

Nach der Einrichtung zeigt die Firmware die Buchstaben automatisch an und kann über die Weboberfläche gesteuert werden.

## Konfiguration

`config.h` enthält Platzhalter-WLAN-Daten, falls noch nichts im EEPROM gespeichert ist. Echte Zugangsdaten sollten **nicht** ins Repository gelangen. Sie können initial über das EEPROM oder die Konfigurationsseite gesetzt werden. Der Parameter `wifi_connect_timeout` bestimmt, wie lange die Verbindung versucht wird (Standard 30 Sekunden).
