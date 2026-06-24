# RiddleMatrix USB Image

Dieses Verzeichnis enthaelt den Builder fuer ein fertiges USB-Stick-Image.

## Ergebnis

Das Image ist fuer zwei Nutzungsarten vorbereitet:

- Unter Windows erscheint eine FAT32-Partition mit `Start-RiddleMatrixWindowsManager.cmd`, dem Windows-Manager und `config/public_ap.env`.
- Beim Booten vom Stick startet ein minimales Debian-System den RiddleMatrix-Hotspot, den Flask-Manager und Firefox im Kiosk-Modus.

Windows startet Programme von USB-Sticks aus Sicherheitsgruenden normalerweise nicht mehr automatisch. `autorun.inf` setzt Label und Start-Aktion, der Manager muss aber je nach Windows-Version per Doppelklick gestartet werden.

## Build-System

Der Build muss auf Linux laufen, zum Beispiel Debian, Ubuntu, WSL2 oder CI. Benoetigt werden root-Rechte und diese Pakete:

```bash
sudo apt-get update
sudo apt-get install -y debootstrap parted util-linux dosfstools e2fsprogs grub-pc-bin grub-efi-amd64-bin xz-utils rsync
```

Image bauen:

```bash
sudo ./USBStick-Image/build-image.sh --output ./RiddleMatrix-usb.img
```

Optional groesser bauen:

```bash
sudo IMAGE_SIZE_MIB=12288 ./USBStick-Image/build-image.sh --output ./RiddleMatrix-usb.img
```

Der Standard ist 8192 MiB, weil Debian, Firefox, X11 und WLAN-Firmware sonst nicht zuverlaessig in das Image passen. Der Builder erzeugt zusaetzlich `RiddleMatrix-usb.img.xz`. Diese komprimierte Datei ist fuer Ablage/Download gedacht. Zum Schreiben mit Win32DiskImager muss sie vorher entpackt werden.

## Auf USB-Stick schreiben

Windows:

- `RiddleMatrix-usb.img.xz` entpacken.
- `RiddleMatrix-usb.img` mit Win32DiskImager, Rufus im DD-Modus oder balenaEtcher auf den Stick schreiben.

Linux:

```bash
sudo dd if=RiddleMatrix-usb.img of=/dev/sdX bs=16M status=progress conv=fsync
```

`/dev/sdX` muss der ganze USB-Stick sein, nicht eine Partition.

## Partitionen

- Partition 1: FAT32 `RIDDLEWIN`, sichtbar unter Windows, enthaelt Windows-Tool und Hotspot-Konfiguration.
- Partition 2: BIOS-GRUB fuer Legacy-Boot.
- Partition 3: Linux-Root `RIDDLE_ROOT`.

UEFI-Boot nutzt die FAT32-Partition als ESP. Legacy-Boot nutzt GRUB im MBR plus BIOS-GRUB-Partition.

## Hotspot-Konfiguration

Die Datei `config/public_ap.env` liegt auf der Windows-sichtbaren FAT32-Partition:

```bash
SSID='RiddleMatrix_AP'
WPA_PASSPHRASE='RiddleMatrix-Setup!'
```

Im Windows-Manager koennen SSID und Passwort gespeichert und mit `USB-Stick WLAN speichern` auf den Stick geschrieben werden. Beim Linux-Boot verwendet `/etc/usbstick/public_ap.env` diese Datei.

## First Boot Resize

Das Image ist bewusst klein. Beim ersten Linux-Boot startet `riddlematrix-grow-root.service` und erweitert die Linux-Root-Partition auf den restlichen USB-Stick. Danach legt der Dienst `/var/lib/riddlematrix-grow-root.done` an und laeuft nicht erneut.
