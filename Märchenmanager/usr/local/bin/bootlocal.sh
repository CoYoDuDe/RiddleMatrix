#!/bin/bash
set -e

PERSIST_ROOT="/mnt/persist"
PERSIST_USR_LOCAL="${PERSIST_ROOT}/usr_local"

if [ -d "$PERSIST_USR_LOCAL" ]; then
    echo "♻️ Synchronisiere persistente /usr/local-Dateien..."
    mkdir -p /usr/local/bin /usr/local/etc
    cp -a "$PERSIST_USR_LOCAL/." /usr/local/
fi

rfkill unblock wifi || true

find_wifi_iface() {
    for iface_path in /sys/class/net/*; do
        [ -e "$iface_path" ] || continue
        iface="$(basename "$iface_path")"
        [ "$iface" = "lo" ] && continue
        if [ -d "$iface_path/wireless" ]; then
            echo "$iface"
            return 0
        fi
        if grep -q "^DEVTYPE=wlan$" "$iface_path/uevent" 2>/dev/null; then
            echo "$iface"
            return 0
        fi
    done
    return 1
}

WIFI_IFACE="$(find_wifi_iface || true)"
if [ -z "$WIFI_IFACE" ]; then
    WIFI_IFACE="wlan0"
    echo "⚠️ Konnte WLAN-Interface nicht automatisch erkennen, verwende Standard: $WIFI_IFACE"
fi

if ! ip link show "$WIFI_IFACE" &>/dev/null; then
    echo "❌ WLAN-Interface $WIFI_IFACE nicht gefunden."
    exit 1
fi

ip link set "$WIFI_IFACE" down || true
ip addr flush dev "$WIFI_IFACE" || true
ip addr add 192.168.10.1/24 dev "$WIFI_IFACE"
ip link set "$WIFI_IFACE" up

cat > /etc/dnsmasq.conf <<EOL
interface=$WIFI_IFACE
bind-interfaces
dhcp-range=192.168.10.100,192.168.10.200,12h
EOL

cat > /etc/hostapd/hostapd.conf <<EOL
interface=$WIFI_IFACE
driver=nl80211
ssid=Traumland_Maerchen
hw_mode=g
channel=6
wpa=2
wpa_passphrase=MaerchenByLothar
EOL

mkdir -p /var/lib/misc
touch /var/lib/misc/dnsmasq.leases
chmod 777 /var/lib/misc/dnsmasq.leases

systemctl restart hostapd
systemctl restart dnsmasq
systemctl restart webserver || true
