#!/bin/bash
# Initialisiere WLAN-Interface
rfkill unblock wifi
WIFI_IFACE=$(iw dev | awk '$1=="Interface"{print $2}' | head -n1)
[ -z "$WIFI_IFACE" ] && WIFI_IFACE=wlan0
ip link set $WIFI_IFACE down
ip addr flush dev $WIFI_IFACE
ip addr add 192.168.10.1/24 dev $WIFI_IFACE
ip link set $WIFI_IFACE up

# Erstelle Konfigurationsdateien
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

# Stelle sicher, dass DHCP-Leases-Verzeichnis existiert
mkdir -p /var/lib/misc
touch /var/lib/misc/dnsmasq.leases
chmod 777 /var/lib/misc/dnsmasq.leases

# Starte systemd-Services
systemctl restart hostapd
systemctl restart dnsmasq
systemctl restart webserver

