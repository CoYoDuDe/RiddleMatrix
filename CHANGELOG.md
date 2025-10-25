# Änderungsprotokoll

## [Unveröffentlicht]
- Bereinigt `loadConfig()` nun auch WLAN-Passwort und Hostnamen, wenn EEPROM-Zellen mit `0xFF`, Leerstrings oder nicht druckbaren Zeichen gefüllt sind, und speichert die Defaults sofort zurück.
- `/shutdown` verlangt jetzt ein gültiges `SHUTDOWN_TOKEN` (außer bei Loopback-Anfragen), protokolliert fehlgeschlagene Versuche
  und liefert JSON-Antworten. Die Weboberfläche fragt das Token ab, bestätigt den Vorgang sichtbar und weist auf die Dauer des
  Herunterfahrens hin.
- Loopback-Ausnahmen akzeptieren nur noch eindeutig lokale Verbindungswege; sobald `X-Forwarded-For` oder `Forwarded`
  nicht ausschließlich Loopback-Adressen enthalten, ist zwingend ein gültiger Administrations-Token erforderlich.
- `/reload_all` erfordert nun dieselbe Authentifizierung wie `/shutdown` (Token oder Loopback). Die Weboberfläche blendet den
  Button ohne gültiges Token aus, bietet einen Dialog zum Hinterlegen des Tokens, bestätigt den Vorgang zusätzlich und räumt
  bei Fehlversuchen gespeicherte Tokens automatisch.
- Automatisierter Flask-Test deckt sowohl den blockierten anonymen Reload als auch den erfolgreichen Token-Aufruf ab.
- `setup.sh` setzt die Lease-Datei `var/lib/misc/dnsmasq.leases` nun mit Eigentümer `root:dnsmasq` auf `0640`, legt sie bei
  Bedarf idempotent an und dokumentiert die Abhängigkeit vom Dienstkonto, damit `dnsmasq` trotz restriktiver Rechte weiterhin
  startet.
- `bootlocal.sh` und `install_public_ap.sh` verwenden dieselben restriktiven Rechte für `dnsmasq.leases`, prüfen auf das
  Vorhandensein der `dnsmasq`-Gruppe und fallen bei Bedarf auf `root:root` zurück; Testlauf beider Skripte bestätigt via
  `ls -l /var/lib/misc/dnsmasq.leases` die erwarteten Besitzer/Rechte.
- Flask-Webserver prüft und normalisiert IPv4-Adressen konsequent (inkl. `devices`-Antworten), ersetzt manipulierte Eingaben
  durch den Platzhalter `0.0.0.0`, erzeugt `<iframe>`-Elemente nur noch per DOM-API und wird durch einen neuen Regressionstest
  gegen bösartige IP-Strings abgesichert.
- Neues 32×32-Bitmap „Sun+Rad“ (`'*'`) kombiniert Sonne und Riesenrad, wird in `loadLetterData()` registriert und besitzt einen
  Host-Test, der Mapping und aktive Pixel verifiziert.
