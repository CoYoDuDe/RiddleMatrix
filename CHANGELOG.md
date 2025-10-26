# Änderungsprotokoll

## [Unveröffentlicht]
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
