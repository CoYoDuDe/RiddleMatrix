# 📋 RiddleMatrix Roadmap

Diese Datei enthält eine Übersicht offener und bereits umgesetzter Aufgaben für die Firmware.

## To‑Do
- [ ] Web‑UI absichern (Authentifizierung, Schutzmechanismen)
- [ ] Zeitsynchronisation per NTP
- [ ] OTA/Remote‑Firmware‑Update
- [ ] Weitere Anzeigeoptionen (Animationen, hochauflösende Symbole)
- [ ] Fehler‑ und Speichermonitoring
- [ ] Code‑Cleanup und Struktur (weniger globale Variablen)
- [ ] CI aufsetzen, die den Arduino‑Code baut
- [ ] Dokumentation für Setup und Troubleshooting erweitern

## Implementierte Features
- [x] RS485‑Trigger mit drei einstellbaren Verzögerungen
- [x] Automatische Buchstabenausgabe in festen Intervallen
- [x] Weboberfläche für WLAN‑Daten, Anzeigeparameter und RTC‑Zeit
- [x] WiFi‑Symbolanzeige und automatisches Abschalten des Webservers bei Verbindungsverlust
- [x] EEPROM‑Speicherung aller Einstellungen inklusive Farben je Wochentag

## Ergänzende Ideen
- [ ] Authentifizierungsoptionen (Passwortschutz oder OAuth)
- [ ] Upload eigener Bitmaps zum Erweitern der Buchstaben/Symbole
- [ ] Weitere Triggerquellen (z.B. HTTP‑API oder MQTT)
- [ ] Mehrsprachige Weboberfläche
