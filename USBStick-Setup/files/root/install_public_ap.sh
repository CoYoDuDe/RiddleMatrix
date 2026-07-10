#!/bin/bash
set -euo pipefail

PUBLIC_AP_DNSMASQ_DROPIN_DEFAULT="/etc/dnsmasq.d/riddlematrix-hotspot.conf"
LEGACY_DNSMASQ_CONFIG="/etc/dnsmasq.conf"
export PUBLIC_AP_DNSMASQ_CONFIG="${PUBLIC_AP_DNSMASQ_CONFIG:-$PUBLIC_AP_DNSMASQ_DROPIN_DEFAULT}"

PUBLIC_AP_HELPER="/usr/local/libexec/public_ap.sh"
if [[ ! -f "$PUBLIC_AP_HELPER" ]]; then
    echo "Missing helper script $PUBLIC_AP_HELPER" >&2
    exit 1
fi

# shellcheck disable=SC1090
source "$PUBLIC_AP_HELPER"
public_ap_write_status "starting" "Hotspot-Konfiguration wird aktualisiert."

notify_legacy_dnsmasq() {
    if [[ -f "$LEGACY_DNSMASQ_CONFIG" && ! -L "$LEGACY_DNSMASQ_CONFIG" ]]; then
        if grep -q "RiddleMatrix-Hotspot" "$LEGACY_DNSMASQ_CONFIG" 2>/dev/null; then
            echo "Legacy hotspot config $LEGACY_DNSMASQ_CONFIG remains unchanged; updating drop-in $PUBLIC_AP_DNSMASQ_CONFIG."
        else
            echo "Existing dnsmasq config $LEGACY_DNSMASQ_CONFIG remains untouched; hotspot settings use drop-in $PUBLIC_AP_DNSMASQ_CONFIG."
        fi
    fi
}

FALLBACK_CREDENTIALS=0
if ! public_ap_load_env; then
    case "$PUBLIC_AP_ENV_STATUS" in
        missing_file|empty_file|missing_ssid|missing_passphrase)
            echo "Warning: $PUBLIC_AP_ENV_ERROR Using default hotspot credentials from $PUBLIC_AP_ENV_TEMPLATE." >&2
            public_ap_apply_defaults
            FALLBACK_CREDENTIALS=1
            ;;
        *)
            echo "Warning: $PUBLIC_AP_ENV_ERROR Hotspot update skipped." >&2
            public_ap_write_status "failed" "$PUBLIC_AP_ENV_ERROR"
            exit 0
            ;;
    esac
fi

if (( FALLBACK_CREDENTIALS )); then
    echo "Default hotspot \"$SSID\" active. Please edit /etc/usbstick/public_ap.env if needed."
fi

echo "Applying hotspot credentials for \"$SSID\"..."

rfkill unblock wifi || true
systemctl stop NetworkManager 2>/dev/null || true
systemctl stop wpa_supplicant 2>/dev/null || true
pkill wpa_supplicant 2>/dev/null || true
public_ap_log_diagnostics

WIFI_IFACE="${1:-}"
if [[ -z "$WIFI_IFACE" ]]; then
    WIFI_IFACE="$(public_ap_detect_wifi_iface || true)"
fi
if [[ -z "$WIFI_IFACE" ]]; then
    echo "No WiFi interface found. Hotspot cannot start." >&2
    public_ap_write_status "failed" "Kein WLAN-Interface gefunden."
    exit 0
fi

if ! ip link show "$WIFI_IFACE" &>/dev/null; then
    echo "WiFi interface $WIFI_IFACE not found." >&2
    public_ap_write_status "failed" "WLAN-Interface $WIFI_IFACE nicht gefunden."
    exit 0
fi

if ! public_ap_iface_supports_ap "$WIFI_IFACE"; then
    echo "WiFi interface $WIFI_IFACE does not report AP mode support. This chipset/driver probably cannot run a Linux hotspot." >&2
    public_ap_write_status "failed" "WLAN-Interface $WIFI_IFACE unterstuetzt keinen AP-Modus."
    exit 0
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
    echo "Group $LEASES_GROUP does not exist; using root:root for $LEASES_FILE" >&2
    LEASES_GROUP="$LEASES_USER"
fi
if [[ -e "$LEASES_FILE" ]]; then
    chmod 0640 "$LEASES_FILE"
else
    install -o "$LEASES_USER" -g "$LEASES_GROUP" -m 0640 /dev/null "$LEASES_FILE"
fi
chown "$LEASES_USER:$LEASES_GROUP" "$LEASES_FILE"

systemctl stop hostapd 2>/dev/null || true
systemctl stop dnsmasq 2>/dev/null || true

if ! systemctl restart hostapd; then
    echo "hostapd failed to start." >&2
    journalctl -u hostapd -n 120 --no-pager 2>/dev/null || true
    public_ap_write_status "failed" "hostapd konnte auf $WIFI_IFACE nicht starten."
    exit 0
fi

if ! systemctl restart dnsmasq; then
    echo "dnsmasq failed to start." >&2
    journalctl -u dnsmasq -n 120 --no-pager 2>/dev/null || true
    public_ap_write_status "failed" "dnsmasq konnte nicht starten."
    exit 0
fi

systemctl restart webserver || true
public_ap_write_status "running" "Hotspot $SSID laeuft auf $WIFI_IFACE mit DHCP 192.168.10.100-200."
echo "Hotspot configuration updated."
