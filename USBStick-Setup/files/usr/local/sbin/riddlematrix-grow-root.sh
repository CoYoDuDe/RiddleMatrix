#!/bin/bash
set -euo pipefail

MARKER="/var/lib/riddlematrix-grow-root.done"

log() {
  printf '[riddlematrix-grow-root] %s\n' "$*"
}

if [[ -e "$MARKER" ]]; then
  log "Root-Dateisystem wurde bereits erweitert."
  exit 0
fi

root_source="$(findmnt -n -o SOURCE /)"
if [[ -z "$root_source" || ! -b "$root_source" ]]; then
  log "Root-Blockdevice konnte nicht ermittelt werden: $root_source"
  exit 0
fi

parent_name="$(lsblk -no PKNAME "$root_source" | head -n1 | tr -d '[:space:]')"
part_number="$(lsblk -no PARTNUM "$root_source" | head -n1 | tr -d '[:space:]')"

if [[ -z "$parent_name" || -z "$part_number" ]]; then
  log "Root-Partition konnte nicht eindeutig erkannt werden."
  exit 0
fi

disk="/dev/$parent_name"
log "Erweitere Partition $part_number auf $disk und danach $root_source."

if command -v growpart >/dev/null 2>&1; then
  growpart "$disk" "$part_number" || true
elif command -v parted >/dev/null 2>&1; then
  parted -s "$disk" "resizepart" "$part_number" "100%" || true
else
  log "Weder growpart noch parted gefunden; Root-Resize wird uebersprungen."
  exit 0
fi

partprobe "$disk" 2>/dev/null || true
udevadm settle 2>/dev/null || true

if command -v resize2fs >/dev/null 2>&1; then
  resize2fs "$root_source" || true
fi

mkdir -p "$(dirname "$MARKER")"
touch "$MARKER"
log "Root-Resize abgeschlossen."
