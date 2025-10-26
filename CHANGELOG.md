# Änderungsprotokoll

## [Unveröffentlicht]
- Bereinigt `loadConfig()` nun auch WLAN-Passwort und Hostnamen, wenn EEPROM-Zellen mit `0xFF`, Leerstrings oder nicht druckbaren Zeichen gefüllt sind, und speichert die Defaults sofort zurück.
- Tokenbasierte Authentifizierung im Firmware-Webserver sowie im SetupHelper entfernt; sämtliche Shutdown- und Verwaltungs-
  Endpunkte stehen innerhalb des Setup-WLANs ohne zusätzliche Header oder Browser-Dialoge zur Verfügung. Frontend und Tests
  spiegeln das neue Verhalten wider.
- Automatisierter Flask-Test prüft den tokenfreien Zugriff und verifiziert weiterhin die Robustheit der Konfigurations-
  Speicherung.
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
- **2024-05-21 – Dokumentationsupdate:** README und TODO beschreiben nun das Offline-Sicherheitskonzept mit temporärem WLAN-Fenster ohne Token-Authentifizierung; dieses Zielbild soll unverändert bestehen bleiben.
