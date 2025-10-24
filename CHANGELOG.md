# Änderungsprotokoll

## [Unveröffentlicht]
- Bereinigt `loadConfig()` nun auch WLAN-Passwort und Hostnamen, wenn EEPROM-Zellen mit `0xFF`, Leerstrings oder nicht druckbaren Zeichen gefüllt sind, und speichert die Defaults sofort zurück.
- `/shutdown` verlangt jetzt ein gültiges `SHUTDOWN_TOKEN` (außer bei Loopback-Anfragen), protokolliert fehlgeschlagene Versuche
  und liefert JSON-Antworten. Die Weboberfläche fragt das Token ab, bestätigt den Vorgang sichtbar und weist auf die Dauer des
  Herunterfahrens hin.
