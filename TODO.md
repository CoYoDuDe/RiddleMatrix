# 📋 RiddleMatrix Roadmap

Diese Datei enthält eine Übersicht offener und bereits umgesetzter Aufgaben für die Firmware.

## To‑Do
- [ ] Web‑UI absichern (Authentifizierung, Schutzmechanismen)
- [ ] OTA/Remote‑Firmware‑Update
- [ ] Weitere Anzeigeoptionen (Animationen, hochauflösende Symbole)
- [ ] Fehler‑ und Speichermonitoring
- [ ] Code‑Cleanup und Struktur (weniger globale Variablen)
- [ ] Dokumentation für Setup und Troubleshooting erweitern

## Implementierte Features
- [x] RS485‑Trigger mit tagesabhängigen Verzögerungsmatrizen
- [x] Automatische Buchstabenausgabe in festen Intervallen
- [x] Mehrspurige Tageskonfiguration (Buchstaben & Farben pro Triggerleitung)
- [x] Weboberfläche für WLAN‑Daten, Anzeigeparameter und RTC‑Zeit
- [x] WiFi‑Symbolanzeige und automatisches Abschalten des Webservers bei Verbindungsverlust
- [x] EEPROM‑Speicherung aller Einstellungen inklusive Farben je Wochentag
- [x] CI aufsetzen, die den Arduino‑Code baut
- [x] Zeitsynchronisation per NTP

## Ergänzende Ideen
- [ ] Authentifizierungsoptionen (Passwortschutz oder OAuth)
- [ ] Upload eigener Bitmaps zum Erweitern der Buchstaben/Symbole
- [ ] Weitere Triggerquellen (z.B. HTTP‑API oder MQTT)
