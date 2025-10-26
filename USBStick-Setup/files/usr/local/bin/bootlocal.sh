#!/bin/bash
set -euo pipefail

PERSIST_ROOT="/mnt/persist"
PERSIST_USR_LOCAL="${PERSIST_ROOT}/usr_local"

PUBLIC_AP_DNSMASQ_DROPIN_DEFAULT="/etc/dnsmasq.d/riddlematrix-hotspot.conf"
LEGACY_DNSMASQ_CONFIG="/etc/dnsmasq.conf"
export PUBLIC_AP_DNSMASQ_CONFIG="${PUBLIC_AP_DNSMASQ_CONFIG:-$PUBLIC_AP_DNSMASQ_DROPIN_DEFAULT}"

PUBLIC_AP_HELPER="/usr/local/libexec/public_ap.sh"
if [[ ! -f "$PUBLIC_AP_HELPER" ]]; then
    echo "❌ Fehlendes Helferskript $PUBLIC_AP_HELPER" >&2
    exit 1
fi

# shellcheck disable=SC1090
source "$PUBLIC_AP_HELPER"

notify_legacy_dnsmasq() {
    if [[ -f "$LEGACY_DNSMASQ_CONFIG" && ! -L "$LEGACY_DNSMASQ_CONFIG" ]]; then
        if grep -q "RiddleMatrix-Hotspot" "$LEGACY_DNSMASQ_CONFIG" 2>/dev/null; then
            echo "ℹ️ Legacy-Hotspot-Konfiguration $LEGACY_DNSMASQ_CONFIG bleibt unverändert; Drop-in $PUBLIC_AP_DNSMASQ_CONFIG wird aktualisiert."
        else
            echo "ℹ️ Bestehende dnsmasq-Konfiguration $LEGACY_DNSMASQ_CONFIG bleibt unangetastet; Drop-in $PUBLIC_AP_DNSMASQ_CONFIG übernimmt die Hotspot-Einstellungen."
        fi
    fi
}

FALLBACK_CREDENTIALS=0
if ! public_ap_load_env; then
    case "$PUBLIC_AP_ENV_STATUS" in
        missing_file|empty_file)
            echo "⚠️ $PUBLIC_AP_ENV_ERROR Verwende Standardwerte (Vorlage: $PUBLIC_AP_ENV_TEMPLATE)." >&2
            public_ap_apply_defaults
            FALLBACK_CREDENTIALS=1
            ;;
        missing_ssid|missing_passphrase)
            echo "⚠️ $PUBLIC_AP_ENV_ERROR Greife auf Standardwerte zurück (Vorlage: $PUBLIC_AP_ENV_TEMPLATE)." >&2
            public_ap_apply_defaults
            FALLBACK_CREDENTIALS=1
            ;;
        *)
            echo "⚠️ $PUBLIC_AP_ENV_ERROR Hotspot-Start wird übersprungen." >&2
            exit 0
            ;;
    esac
fi

if (( FALLBACK_CREDENTIALS )); then
    echo "ℹ️ Standard-Hotspot \"$SSID\" aktiv. Bitte /etc/usbstick/public_ap.env anpassen (Vorlage: $PUBLIC_AP_ENV_TEMPLATE)."
fi

if [[ -d "$PERSIST_USR_LOCAL" ]]; then
    echo "♻️ Synchronisiere persistente /usr/local-Dateien..."
    mkdir -p /usr/local/bin /usr/local/etc
    cp -a "$PERSIST_USR_LOCAL/." /usr/local/
fi

rfkill unblock wifi || true

WIFI_IFACE="$(public_ap_detect_wifi_iface || true)"
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
notify_legacy_dnsmasq
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
