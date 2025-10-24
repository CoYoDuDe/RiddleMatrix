# Änderungsprotokoll

## [Unveröffentlicht]
- Bereinigt `loadConfig()` nun auch WLAN-Passwort und Hostnamen, wenn EEPROM-Zellen mit `0xFF`, Leerstrings oder nicht druckbaren Zeichen gefüllt sind, und speichert die Defaults sofort zurück.
- `/shutdown` verlangt jetzt ein gültiges `SHUTDOWN_TOKEN` (außer bei Loopback-Anfragen), protokolliert fehlgeschlagene Versuche
  und liefert JSON-Antworten. Die Weboberfläche fragt das Token ab, bestätigt den Vorgang sichtbar und weist auf die Dauer des
  Herunterfahrens hin.
- `/reload_all` erfordert nun dieselbe Authentifizierung wie `/shutdown` (Token oder Loopback). Die Weboberfläche blendet den
  Button ohne gültiges Token aus, bietet einen Dialog zum Hinterlegen des Tokens, bestätigt den Vorgang zusätzlich und räumt
  bei Fehlversuchen gespeicherte Tokens automatisch.
- Automatisierter Flask-Test deckt sowohl den blockierten anonymen Reload als auch den erfolgreichen Token-Aufruf ab.
