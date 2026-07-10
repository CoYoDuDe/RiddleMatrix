#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

OUTPUT_IMAGE="$REPO_ROOT/RiddleMatrix-usb.img"
IMAGE_SIZE_MIB="${IMAGE_SIZE_MIB:-8192}"
DEBIAN_SUITE="${DEBIAN_SUITE:-trixie}"
DEBIAN_MIRROR="${DEBIAN_MIRROR:-https://deb.debian.org/debian}"
COMPRESS=1
work_dir=""
loop_device=""
root_mount=""
fat_mount=""

usage() {
  cat <<'USAGE'
Usage: sudo USBStick-Image/build-image.sh [options]

Options:
  -o, --output <file>       Output raw image path (default: ./RiddleMatrix-usb.img)
      --size-mib <mib>      Initial image size in MiB (default: 8192)
      --suite <name>        Debian suite (default: trixie)
      --mirror <url>        Debian mirror URL
      --no-compress         Do not create .xz next to the raw image
  -h, --help                Show this help

The image is intentionally small. On first Linux boot the root partition grows
to the full USB stick size.
USAGE
}

log() {
  printf '[image-build] %s\n' "$*"
}

die() {
  printf '[image-build] ERROR: %s\n' "$*" >&2
  exit 1
}

require_root() {
  [[ $(id -u) -eq 0 ]] || die "Dieses Skript muss als root laufen."
}

require_commands() {
  local missing=()
  for cmd in debootstrap parted partprobe losetup mkfs.vfat mkfs.ext4 blkid chroot rsync xz; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
      missing+=("$cmd")
    fi
  done
  if [[ ${#missing[@]} -gt 0 ]]; then
    die "Fehlende Build-Abhaengigkeiten: ${missing[*]}"
  fi
}

parse_args() {
  while [[ $# -gt 0 ]]; do
    case "$1" in
      -o|--output)
        shift || die "Missing argument for --output"
        OUTPUT_IMAGE="$1"
        ;;
      --size-mib)
        shift || die "Missing argument for --size-mib"
        IMAGE_SIZE_MIB="$1"
        ;;
      --suite)
        shift || die "Missing argument for --suite"
        DEBIAN_SUITE="$1"
        ;;
      --mirror)
        shift || die "Missing argument for --mirror"
        DEBIAN_MIRROR="$1"
        ;;
      --no-compress)
        COMPRESS=0
        ;;
      -h|--help)
        usage
        exit 0
        ;;
      *)
        die "Unknown option: $1"
        ;;
    esac
    shift || break
  done
}

write_public_ap_env() {
  local output=$1
  local ssid=${RIDDLEMATRIX_PUBLIC_AP_SSID:-RiddleMatrix_AP}
  local passphrase=${RIDDLEMATRIX_PUBLIC_AP_PASSWORD:-RiddleMatrix-Setup!}
  mkdir -p "$(dirname "$output")"
  cat > "$output" <<EOF
# RiddleMatrix Hotspot-Konfiguration
# Diese Datei kann unter Windows mit dem RiddleMatrix Windows Manager angepasst werden.
SSID='$ssid'
WPA_PASSPHRASE='$passphrase'
EOF
}

write_windows_start_files() {
  local fat_mount=$1
  cat > "$fat_mount/Start-RiddleMatrixWindowsManager.cmd" <<'EOF'
@echo off
cd /d "%~dp0"
set "EXE=%~dp0WindowsTool\RiddleMatrixWindowsManager.exe"
if exist "%EXE%" (
    start "" "%EXE%"
) else (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0WindowsTool\Start-RiddleMatrixWindowsManager.ps1"
)
EOF
  cat > "$fat_mount/autorun.inf" <<'EOF'
[AutoRun]
label=RiddleMatrix
open=Start-RiddleMatrixWindowsManager.cmd
action=RiddleMatrix Windows Manager starten
EOF
  cat > "$fat_mount/RIDDLEMATRIX_USB.txt" <<'EOF'
RiddleMatrix USB stick

Windows: Start-RiddleMatrixWindowsManager.cmd ausfuehren.
Boot: Im BIOS/UEFI vom USB-Stick starten.
EOF
}

copy_windows_payload() {
  local fat_mount=$1
  mkdir -p "$fat_mount/config"
  write_public_ap_env "$fat_mount/config/public_ap.env"
  write_windows_start_files "$fat_mount"

  rsync -a --delete \
    --exclude='.git/' \
    --exclude='.pytest_cache/' \
    --exclude='RiddleMatrix-usb.img' \
    --exclude='RiddleMatrix-usb.img.xz' \
    "$REPO_ROOT/WindowsTool" "$fat_mount/"
  rsync -a --delete "$REPO_ROOT/USBStick-Setup" "$fat_mount/"
  install -m 0644 "$REPO_ROOT/README.md" "$fat_mount/README-RiddleMatrix.md"
}

configure_base_system() {
  local root_mount=$1 fat_uuid=$2 root_uuid=$3

  cat > "$root_mount/etc/apt/sources.list" <<EOF
deb $DEBIAN_MIRROR $DEBIAN_SUITE main contrib non-free non-free-firmware
deb $DEBIAN_MIRROR $DEBIAN_SUITE-updates main contrib non-free non-free-firmware
deb https://security.debian.org/debian-security $DEBIAN_SUITE-security main contrib non-free non-free-firmware
EOF

  cat > "$root_mount/etc/fstab" <<EOF
UUID=$root_uuid / ext4 defaults,noatime 0 1
UUID=$fat_uuid /riddlematrix vfat defaults,umask=0077,nofail 0 2
EOF

  echo "riddlematrix-usb" > "$root_mount/etc/hostname"
  echo "127.0.0.1 localhost" > "$root_mount/etc/hosts"
  echo "127.0.1.1 riddlematrix-usb" >> "$root_mount/etc/hosts"

  mkdir -p "$root_mount/etc/initramfs-tools/conf.d" "$root_mount/etc/default/grub.d"
  cat > "$root_mount/etc/initramfs-tools/conf.d/riddlematrix-usb.conf" <<'EOF'
# USB sticks boot on very different chipsets. Keep storage/USB modules in the
# initramfs instead of relying on the build machine's detected hardware.
MODULES=most
EOF
  cat > "$root_mount/etc/default/grub.d/riddlematrix-usb.cfg" <<'EOF'
GRUB_CMDLINE_LINUX_DEFAULT="quiet rootwait"
GRUB_CMDLINE_LINUX="rootwait"
EOF

  mkdir -p "$root_mount/riddlematrix" "$root_mount/etc/usbstick"
}

mount_chroot_filesystems() {
  local root_mount=$1
  mount --bind /dev "$root_mount/dev"
  mount --bind /dev/pts "$root_mount/dev/pts"
  mount -t proc proc "$root_mount/proc"
  mount -t sysfs sys "$root_mount/sys"
}

umount_if_mounted() {
  local path=$1
  if mountpoint -q "$path"; then
    umount "$path"
  fi
}

main() {
  parse_args "$@"
  require_root
  require_commands

  work_dir="$(mktemp -d)"
  root_mount="$work_dir/root"
  fat_mount="$work_dir/fat"
  mkdir -p "$root_mount" "$fat_mount"

cleanup() {
    set +e
    if [[ -n ${root_mount:-} ]]; then
      umount_if_mounted "$root_mount/boot/efi"
      umount_if_mounted "$root_mount/dev/pts"
      umount_if_mounted "$root_mount/dev"
      umount_if_mounted "$root_mount/proc"
      umount_if_mounted "$root_mount/sys"
      umount_if_mounted "$root_mount/riddlematrix"
      umount_if_mounted "$root_mount"
    fi
    if [[ -n ${fat_mount:-} ]]; then
      umount_if_mounted "$fat_mount"
    fi
    if [[ -n ${loop_device:-} ]]; then
      losetup -d "$loop_device" 2>/dev/null || true
    fi
    if [[ -n ${work_dir:-} ]]; then
      rm -rf "$work_dir"
    fi
  }
  trap cleanup EXIT

  log "Erzeuge Raw-Image $OUTPUT_IMAGE (${IMAGE_SIZE_MIB} MiB)."
  rm -f "$OUTPUT_IMAGE" "$OUTPUT_IMAGE.xz"
  truncate -s "${IMAGE_SIZE_MIB}M" "$OUTPUT_IMAGE"

  parted -s "$OUTPUT_IMAGE" mklabel gpt
  parted -s "$OUTPUT_IMAGE" mkpart RIDDLEWIN fat32 1MiB 769MiB
  parted -s "$OUTPUT_IMAGE" set 1 esp on
  parted -s "$OUTPUT_IMAGE" mkpart BIOSGRUB 769MiB 773MiB
  parted -s "$OUTPUT_IMAGE" set 2 bios_grub on
  parted -s "$OUTPUT_IMAGE" mkpart RIDDLELINUX ext4 773MiB 100%

  loop_device="$(losetup --show -Pf "$OUTPUT_IMAGE")"
  partprobe "$loop_device" || true
  sleep 1

  local part_prefix="${loop_device}p"
  if [[ "$loop_device" =~ [0-9]$ ]]; then
    part_prefix="${loop_device}p"
  fi
  local fat_part="${part_prefix}1"
  local root_part="${part_prefix}3"

  mkfs.vfat -F 32 -n RIDDLEWIN "$fat_part"
  mkfs.ext4 -F -L RIDDLE_ROOT "$root_part"

  mount "$root_part" "$root_mount"
  mkdir -p "$root_mount/boot/efi"
  mount "$fat_part" "$root_mount/boot/efi"

  log "Installiere Debian-Basissystem."
  debootstrap --arch=amd64 "$DEBIAN_SUITE" "$root_mount" "$DEBIAN_MIRROR"
  export DEBIAN_FRONTEND=noninteractive

  local fat_uuid root_uuid
  fat_uuid="$(blkid -s UUID -o value "$fat_part")"
  root_uuid="$(blkid -s UUID -o value "$root_part")"
  configure_base_system "$root_mount" "$fat_uuid" "$root_uuid"

  mount_chroot_filesystems "$root_mount"
  echo "grub-pc grub-pc/install_devices_empty boolean true" | chroot "$root_mount" debconf-set-selections
  chroot "$root_mount" apt-get update
  chroot "$root_mount" apt-get install -y \
    systemd-sysv linux-image-amd64 initramfs-tools \
    grub-common grub2-common grub-pc-bin grub-efi-amd64-bin \
    dosfstools e2fsprogs sudo ca-certificates pciutils usbutils

  local -a optional_wifi_packages=(
    firmware-linux-free
    firmware-linux-nonfree
    firmware-misc-nonfree
    firmware-amd-graphics
    firmware-iwlwifi
    firmware-realtek
    firmware-atheros
    firmware-ath9k-htc
    firmware-brcm80211
    firmware-libertas
    firmware-mediatek
    firmware-zd1211
    wireless-regdb
    iw
    usb-modeswitch
  )
  local -a available_wifi_packages=()
  local package
  for package in "${optional_wifi_packages[@]}"; do
    if chroot "$root_mount" apt-cache show "$package" >/dev/null 2>&1; then
      available_wifi_packages+=("$package")
    else
      log "Optionales WLAN-Paket nicht verfuegbar: $package"
    fi
  done
  if [[ ${#available_wifi_packages[@]} -gt 0 ]]; then
    chroot "$root_mount" apt-get install -y "${available_wifi_packages[@]}"
  fi

  log "Installiere RiddleMatrix-Payload und Pakete."
  "$REPO_ROOT/USBStick-Setup/setup.sh" --target "$root_mount" --skip-systemd
  # These package units may be enabled by default. bootlocal.service owns the
  # AP lifecycle and starts hostapd/dnsmasq only after the WiFi interface was
  # detected and configured.
  chroot "$root_mount" systemctl disable dnsmasq.service hostapd.service wpa_supplicant.service 2>/dev/null || true

  if ! chroot "$root_mount" id kioskuser >/dev/null 2>&1; then
    chroot "$root_mount" useradd -m -s /bin/bash kioskuser
  fi
  chroot "$root_mount" chown -R kioskuser:kioskuser /home/kioskuser
  chroot "$root_mount" systemctl enable systemd-networkd.service
  chroot "$root_mount" systemctl enable bootlocal.service webserver.service riddlematrix-grow-root.service getty@tty1.service

  log "Installiere GRUB fuer Legacy-BIOS und UEFI."
  chroot "$root_mount" update-initramfs -u -k all
  grub-install --target=i386-pc --boot-directory="$root_mount/boot" "$loop_device"
  chroot "$root_mount" grub-install --target=x86_64-efi --efi-directory=/boot/efi --bootloader-id=RiddleMatrix --removable --recheck
  chroot "$root_mount" update-grub

  umount_if_mounted "$root_mount/boot/efi"
  mkdir -p "$root_mount/riddlematrix"
  mount "$fat_part" "$root_mount/riddlematrix"
  copy_windows_payload "$root_mount/riddlematrix"
  rm -f "$root_mount/etc/usbstick/public_ap.env"
  ln -s /riddlematrix/config/public_ap.env "$root_mount/etc/usbstick/public_ap.env"
  sync

  # Compress only after every filesystem is cleanly unmounted. Compressing a
  # still-mounted raw image can produce an .xz that does not match the final
  # image after delayed filesystem metadata is flushed during unmount.
  umount_if_mounted "$root_mount/riddlematrix"
  umount_if_mounted "$root_mount/dev/pts"
  umount_if_mounted "$root_mount/dev"
  umount_if_mounted "$root_mount/proc"
  umount_if_mounted "$root_mount/sys"
  umount_if_mounted "$root_mount"
  losetup -d "$loop_device"
  loop_device=""

  log "Image fertig: $OUTPUT_IMAGE"
  if ((COMPRESS)); then
    log "Komprimiere Image nach $OUTPUT_IMAGE.xz"
    xz -T0 -zk "$OUTPUT_IMAGE"
  fi
}

main "$@"
