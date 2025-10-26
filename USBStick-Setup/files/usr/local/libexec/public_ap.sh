#!/bin/bash
# shellcheck shell=bash

PUBLIC_AP_ENV_FILE=${PUBLIC_AP_ENV_FILE:-/etc/usbstick/public_ap.env}
PUBLIC_AP_HOSTAPD_DIR=${PUBLIC_AP_HOSTAPD_DIR:-/etc/hostapd}
PUBLIC_AP_HOSTAPD_CONFIG=${PUBLIC_AP_HOSTAPD_CONFIG:-${PUBLIC_AP_HOSTAPD_DIR}/hostapd.conf}
PUBLIC_AP_HOSTAPD_TEMPLATE=${PUBLIC_AP_HOSTAPD_TEMPLATE:-${PUBLIC_AP_HOSTAPD_DIR}/hostapd.conf.template}
PUBLIC_AP_DNSMASQ_CONFIG=${PUBLIC_AP_DNSMASQ_CONFIG:-/etc/dnsmasq.conf}

PUBLIC_AP_DEFAULT_SSID=${PUBLIC_AP_DEFAULT_SSID:-"RiddleMatrix-Hotspot"}
PUBLIC_AP_DEFAULT_WPA_PASSPHRASE=${PUBLIC_AP_DEFAULT_WPA_PASSPHRASE:-"BittePasswortAnpassen123!"}

PUBLIC_AP_ENV_STATUS="uninitialized"
PUBLIC_AP_ENV_ERROR=""

public_ap_log() {
    printf '%s\n' "$*"
}

public_ap_apply_defaults() {
    SSID=$PUBLIC_AP_DEFAULT_SSID
    WPA_PASSPHRASE=$PUBLIC_AP_DEFAULT_WPA_PASSPHRASE
    PUBLIC_AP_ENV_STATUS="defaults"
    PUBLIC_AP_ENV_ERROR=""
}

public_ap_load_env() {
    local env_file=${1:-$PUBLIC_AP_ENV_FILE}
    PUBLIC_AP_ENV_STATUS="error"
    PUBLIC_AP_ENV_ERROR=""

    if [[ ! -f "$env_file" ]]; then
        PUBLIC_AP_ENV_STATUS="missing_file"
        PUBLIC_AP_ENV_ERROR="Umgebungsdatei $env_file fehlt."
        return 1
    fi
    if [[ ! -s "$env_file" ]]; then
        PUBLIC_AP_ENV_STATUS="empty_file"
        PUBLIC_AP_ENV_ERROR="Umgebungsdatei $env_file ist leer."
        return 1
    fi
    # shellcheck disable=SC1090
    source "$env_file"

    if [[ -z ${SSID:-} ]]; then
        PUBLIC_AP_ENV_STATUS="missing_ssid"
        PUBLIC_AP_ENV_ERROR="SSID ist nicht gesetzt (Datei: $env_file)."
        return 1
    fi
    if [[ -z ${WPA_PASSPHRASE:-} ]]; then
        PUBLIC_AP_ENV_STATUS="missing_passphrase"
        PUBLIC_AP_ENV_ERROR="WPA_PASSPHRASE ist nicht gesetzt (Datei: $env_file)."
        return 1
    fi
    local pass_length=${#WPA_PASSPHRASE}
    if ((pass_length < 8 || pass_length > 63)); then
        PUBLIC_AP_ENV_STATUS="invalid_passphrase_length"
        PUBLIC_AP_ENV_ERROR="WPA_PASSPHRASE muss zwischen 8 und 63 Zeichen lang sein (aktuell: $pass_length)."
        return 1
    fi

    PUBLIC_AP_ENV_STATUS="ok"
    PUBLIC_AP_ENV_ERROR=""
}

public_ap_detect_wifi_iface() {
    local iface_path iface
    for iface_path in /sys/class/net/*; do
        [[ -e $iface_path ]] || continue
        iface=$(basename "$iface_path")
        [[ $iface == lo ]] && continue
        if [[ -d $iface_path/wireless ]]; then
            printf '%s\n' "$iface"
            return 0
        fi
        if grep -q "^DEVTYPE=wlan$" "$iface_path/uevent" 2>/dev/null; then
            printf '%s\n' "$iface"
            return 0
        fi
    done
    return 1
}

public_ap_escape_sed() {
    printf '%s' "$1" | sed -e 's/[\\&|]/\\&/g'
}

public_ap_render_template() {
    local template=$1 output=$2
    shift 2
    if [[ ! -f $template ]]; then
        public_ap_log "❌ Template $template wurde nicht gefunden."
        return 1
    fi
    local -a sed_args=()
    while [[ $# -gt 0 ]]; do
        local placeholder=$1 value=$2
        shift 2
        sed_args+=(-e "s|$placeholder|$(public_ap_escape_sed "$value")|g")
    done
    sed "${sed_args[@]}" "$template" > "$output"
}

public_ap_render_hostapd_config() {
    local wifi_iface=$1
    local config=${2:-$PUBLIC_AP_HOSTAPD_CONFIG}
    local template=${3:-$PUBLIC_AP_HOSTAPD_TEMPLATE}
    mkdir -p "$(dirname "$config")"

    if [[ -f $config && -f $template ]]; then
        :
    elif [[ -f $config ]] && grep -q '@SSID@' "$config"; then
        cp "$config" "$template"
    elif [[ -f $config && ! -f $template ]]; then
        local tmp_template
        tmp_template=$(mktemp)
        sed \
            -e 's/^interface=.*/interface=@WIFI_IFACE@/' \
            -e 's/^ssid=.*/ssid=@SSID@/' \
            -e 's/^wpa_passphrase=.*/wpa_passphrase=@WPA_PASSPHRASE@/' \
            "$config" > "$tmp_template"
        mv "$tmp_template" "$template"
    fi

    if [[ ! -f $template ]]; then
        public_ap_log "❌ Hostapd-Template $template wurde nicht gefunden."
        return 1
    fi

    local tmp
    tmp=$(mktemp)
    public_ap_render_template "$template" "$tmp" \
        '@SSID@' "$SSID" \
        '@WPA_PASSPHRASE@' "$WPA_PASSPHRASE" \
        '@WIFI_IFACE@' "$wifi_iface"
    install -m 0600 "$tmp" "$config"
    rm -f "$tmp"
}

public_ap_write_dnsmasq_config() {
    local wifi_iface=$1
    local config=${2:-$PUBLIC_AP_DNSMASQ_CONFIG}
    mkdir -p "$(dirname "$config")"
    cat > "$config" <<EOF
# DHCP-Konfiguration für Hotspot "$SSID"
interface=$wifi_iface
bind-interfaces
dhcp-range=192.168.10.100,192.168.10.200,12h
EOF
}
