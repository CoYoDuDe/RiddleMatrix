#!/bin/bash
set -euo pipefail

PUBLIC_AP_HELPER="/usr/local/libexec/public_ap.sh"
if [[ ! -f "$PUBLIC_AP_HELPER" ]]; then
    echo "❌ Fehlendes Helferskript $PUBLIC_AP_HELPER" >&2
    exit 1
fi

# shellcheck disable=SC1090
source "$PUBLIC_AP_HELPER"

FALLBACK_CREDENTIALS=0
if ! public_ap_load_env; then
    case "$PUBLIC_AP_ENV_STATUS" in
        missing_file|empty_file)
            echo "⚠️ $PUBLIC_AP_ENV_ERROR Hotspot wird mit Standardwerten gestartet." >&2
            public_ap_apply_defaults
            FALLBACK_CREDENTIALS=1
            ;;
        missing_ssid|missing_passphrase)
            echo "⚠️ $PUBLIC_AP_ENV_ERROR Verwende Standardwerte aus dem Installer." >&2
            public_ap_apply_defaults
            FALLBACK_CREDENTIALS=1
            ;;
        *)
            echo "⚠️ $PUBLIC_AP_ENV_ERROR Hotspot-Installation wird übersprungen." >&2
            exit 0
            ;;
    esac
fi

if (( FALLBACK_CREDENTIALS )); then
    echo "ℹ️ Standard-Hotspot \"$SSID\" aktiv. Bitte /etc/usbstick/public_ap.env anpassen."
fi

echo "🔐 Wende Zugangsdaten für Hotspot \"$SSID\" an..."

WIFI_IFACE="${1:-}"
if [[ -z "$WIFI_IFACE" ]]; then
    WIFI_IFACE="$(public_ap_detect_wifi_iface || true)"
fi
if [[ -z "$WIFI_IFACE" ]]; then
    WIFI_IFACE="wlan0"
    echo "⚠️ Konnte WLAN-Interface nicht automatisch erkennen, verwende Standard: $WIFI_IFACE"
fi

if ! ip link show "$WIFI_IFACE" &>/dev/null; then
    echo "❌ WLAN-Interface $WIFI_IFACE nicht gefunden." >&2
    exit 1
fi

ip link set "$WIFI_IFACE" down || true
ip addr flush dev "$WIFI_IFACE" || true
ip addr add 192.168.10.1/24 dev "$WIFI_IFACE"
ip link set "$WIFI_IFACE" up

public_ap_write_dnsmasq_config "$WIFI_IFACE"
public_ap_render_hostapd_config "$WIFI_IFACE"

mkdir -p /var/lib/misc
LEASES_FILE="/var/lib/misc/dnsmasq.leases"
LEASES_USER="root"
LEASES_GROUP="dnsmasq"
if ! getent group "$LEASES_GROUP" >/dev/null 2>&1; then
    echo "⚠️ Gruppe $LEASES_GROUP nicht vorhanden; verwende root:root für $LEASES_FILE" >&2
    LEASES_GROUP="$LEASES_USER"
fi
if [[ -e "$LEASES_FILE" ]]; then
    chmod 0640 "$LEASES_FILE"
else
    install -o "$LEASES_USER" -g "$LEASES_GROUP" -m 0640 /dev/null "$LEASES_FILE"
fi
chown "$LEASES_USER:$LEASES_GROUP" "$LEASES_FILE"

systemctl restart hostapd
systemctl restart dnsmasq
systemctl restart webserver || true

echo "✅ Hotspot-Konfiguration wurde aktualisiert."
