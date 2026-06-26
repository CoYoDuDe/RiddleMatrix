# RiddleMatrix Roadmap

Diese Datei beschreibt den aktuellen Projektstand und die noch offenen Arbeiten. Der Fokus liegt auf Firmware, Windows-Manager, USB-Image und Webspace-Manager.

## Erledigt

- [x] Firmware-Builds fuer NodeMCU 0.9 (`nodemcu`), NodeMCU 1.0 (`nodemcuv2`) und ESP32 (`esp32dev`).
- [x] Weboberflaeche fuer Netzwerk, Anzeige, Trigger, Farben, RTC-Zeit und Aktivzeitfenster.
- [x] RS485-Trigger mit tagesabhaengigen Zeichen/Symbolen und Verzogerungen.
- [x] Mehrspurige Tageskonfiguration fuer bis zu drei Trigger.
- [x] Standardmodus mit Manager-Hotspot und automatischem WLAN-Abschalten nach 5 Minuten Inaktivitaet.
- [x] Dauer-WLAN-Modus mit DHCP oder fester IP.
- [x] AP+STA/Fallback-Modus mit lokaler Box-AP-Konfiguration.
- [x] Kein WiFi-Symbol im Dauer-WLAN/AP+STA-Betrieb; WiFi-Symbol nur im zeitlich begrenzten Standard-WLAN-Fenster.
- [x] Automatische NTP-Zeitsynchronisierung bei Internetverbindung.
- [x] Aktivzeit/Standby: Standard 10:00 bis 18:05 Uhr, ausserhalb wird nicht automatisch angezeigt.
- [x] EEPROM-Speicherung fuer WLAN, Trigger, Farben, Farbmodi, Verzogerungen, Aktivzeiten und Zeichen/Symbole.
- [x] Bearbeitbare Zeichen/Symbole: A-Z, Sun, WIFI, Rad, Riddler sowie Zusatzzeichen 0 bis 7.
- [x] Symbol-Editor im Manager mit 32x32-Raster, Namen, Vorlagen und Bildimport.
- [x] Zentrale Uebertragung aller Zeichen/Symbole an alle bekannten Boxen mit Abbrechen-Funktion.
- [x] Zentrale Uebertragung der Box-Einstellungen an alle bekannten Boxen mit Fortschrittsanzeige.
- [x] Windows-Manager als Standalone-EXE.
- [x] Windows-Manager mit AP-Start, vorhandene WLAN-Verbindung vorher abfragen, Manager-only und Stoppen.
- [x] USB-Image mit Legacy-BIOS- und UEFI-Boot, Linux-Kiosk, Windows-Dateien und Grow-Root-Service.
- [x] Alte Shutdown-Absicherung entfernt; Shutdown bleibt nur im gebooteten USB-Stick-Kontext sichtbar.
- [x] CI/Testabdeckung fuer Webserver- und EEPROM-Layouts erweitert.

## Noch offen

- [ ] Echte Hardware-Tests auf ESP8266-Boxen durchfuehren.
- [ ] Echte Hardware-Tests auf ESP32-Boxen durchfuehren.
- [ ] WLAN-Modi mit echten Boxen pruefen: Standard, Dauer-WLAN, AP+STA/Fallback, feste IP und DHCP.
- [ ] Symbol-Editor mit echten Boxen pruefen: alle Zeichen/Symbole speichern, uebertragen und nach Neustart erneut laden.
- [ ] Standby/Aktivzeit mit echter RTC und NTP-Korrektur pruefen.
- [ ] USB-Image auf mindestens einem Legacy-BIOS-Notebook und einem reinen UEFI-Notebook booten.
- [ ] Windows-Manager auf einem frischen Windows-System ohne Projektordner testen.
- [ ] OTA-/Remote-Firmware-Update planen und implementieren, falls spaeter wirklich benoetigt.
- [ ] Optionales Monitoring fuer freien Heap, EEPROM-/Flash-Nutzung und letzte Fehler im Webinterface.
- [ ] Code-Cleanup: globale Variablen weiter reduzieren und Firmware-Module staerker kapseln.
- [ ] Setup- und Troubleshooting-Dokumentation mit Screenshots erweitern.

## Bewusst begrenzt

- [ ] Mehr als acht Zusatzzeichen werden aktuell nicht eingeplant. A-Z und Standardsymbole sind bereits bearbeitbar; die Zusatzzeichen 0 bis 7 sind zusaetzliche frei benennbare Zeichen. Mehr permanente Zusatzzeichen wuerden auf ESP8266 unnoetig Flash/RAM und EEPROM-Layout belasten.
- [ ] Browser vom externen Webspace kann den lokalen LAN-IP-Bereich nicht verlaesslich automatisch erkennen. Der Manager schlaegt Kandidaten vor und scannt nur Geraete, die wie RiddleMatrix-Boxen antworten.
