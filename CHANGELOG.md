# Aenderungsprotokoll

## [Unveroeffentlicht]
- `setup.sh` setzt die Lease-Datei `var/lib/misc/dnsmasq.leases` mit Eigentuemer `root:dnsmasq` auf `0640`, legt sie bei Bedarf idempotent an und dokumentiert die Abhaengigkeit vom Dienstkonto.
- `bootlocal.sh` und `install_public_ap.sh` verwenden dieselben restriktiven Rechte fuer `dnsmasq.leases`, pruefen auf die `dnsmasq`-Gruppe und fallen bei Bedarf auf `root:root` zurueck.
- Flask-Webserver prueft und normalisiert IPv4-Adressen konsequent, ersetzt manipulierte Eingaben durch `0.0.0.0` und erzeugt `<iframe>`-Elemente nur noch per DOM-API.
- Die alte `letters.h`/`letterData`-Map wurde durch `symbol_defaults.h` mit Factory-Lookup ersetzt; aktive Zeichen/Symbole werden zuerst aus gespeicherten Overrides geladen.
