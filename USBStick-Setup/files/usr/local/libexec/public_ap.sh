#!/bin/bash
# shellcheck shell=bash

PUBLIC_AP_ENV_FILE=${PUBLIC_AP_ENV_FILE:-/etc/usbstick/public_ap.env}
PUBLIC_AP_ENV_TEMPLATE=${PUBLIC_AP_ENV_TEMPLATE:-/etc/usbstick/public_ap.env.example}
PUBLIC_AP_HOSTAPD_DIR=${PUBLIC_AP_HOSTAPD_DIR:-/etc/hostapd}
PUBLIC_AP_HOSTAPD_CONFIG=${PUBLIC_AP_HOSTAPD_CONFIG:-${PUBLIC_AP_HOSTAPD_DIR}/hostapd.conf}
PUBLIC_AP_HOSTAPD_TEMPLATE=${PUBLIC_AP_HOSTAPD_TEMPLATE:-${PUBLIC_AP_HOSTAPD_DIR}/hostapd.conf.template}
PUBLIC_AP_DNSMASQ_CONFIG=${PUBLIC_AP_DNSMASQ_CONFIG:-/etc/dnsmasq.d/riddlematrix-hotspot.conf}
PUBLIC_AP_STATUS_FILE=${PUBLIC_AP_STATUS_FILE:-/run/riddlematrix-hotspot.status}

PUBLIC_AP_DEFAULT_SSID=${PUBLIC_AP_DEFAULT_SSID:-"RiddleMatrix_AP"}
PUBLIC_AP_DEFAULT_WPA_PASSPHRASE=${PUBLIC_AP_DEFAULT_WPA_PASSPHRASE:-"RiddleMatrix-Setup!"}

PUBLIC_AP_ENV_STATUS="uninitialized"
PUBLIC_AP_ENV_ERROR=""

public_ap_log() {
    printf '%s\n' "$*"
}

public_ap_write_status() {
    local state=$1
    local message=${2:-}
    local interfaces=""
    local ap_capable=""
    local drivers=""
    local iface driver

    while IFS= read -r iface; do
        [[ -n $iface ]] || continue
        interfaces+="${interfaces:+,}$iface"
        driver=$(basename "$(readlink -f "/sys/class/net/$iface/device/driver" 2>/dev/null)" 2>/dev/null || true)
        [[ -n $driver ]] || driver="unbekannt"
        drivers+="${drivers:+,}$iface:$driver"
        if public_ap_iface_supports_ap "$iface"; then
            ap_capable+="${ap_capable:+,}$iface"
        fi
    done < <(public_ap_wifi_interfaces)

    message=${message//$'\n'/ }
    message=${message//$'\r'/ }
    mkdir -p "$(dirname "$PUBLIC_AP_STATUS_FILE")"
    {
        printf 'STATE=%s\n' "$state"
        printf 'MESSAGE=%s\n' "$message"
        printf 'INTERFACES=%s\n' "$interfaces"
        printf 'AP_CAPABLE=%s\n' "$ap_capable"
        printf 'DRIVERS=%s\n' "$drivers"
        printf 'UPDATED=%s\n' "$(date -Is 2>/dev/null || date)"
    } > "$PUBLIC_AP_STATUS_FILE"
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
        PUBLIC_AP_ENV_ERROR="Environment file $env_file is missing."
        return 1
    fi
    if [[ ! -s "$env_file" ]]; then
        PUBLIC_AP_ENV_STATUS="empty_file"
        PUBLIC_AP_ENV_ERROR="Environment file $env_file is empty."
        return 1
    fi

    # shellcheck disable=SC1090
    source "$env_file"

    if [[ -z ${SSID:-} ]]; then
        PUBLIC_AP_ENV_STATUS="missing_ssid"
        PUBLIC_AP_ENV_ERROR="SSID is not set in $env_file."
        return 1
    fi
    if [[ -z ${WPA_PASSPHRASE:-} ]]; then
        PUBLIC_AP_ENV_STATUS="missing_passphrase"
        PUBLIC_AP_ENV_ERROR="WPA_PASSPHRASE is not set in $env_file."
        return 1
    fi

    local pass_length=${#WPA_PASSPHRASE}
    if (( pass_length < 8 || pass_length > 63 )); then
        PUBLIC_AP_ENV_STATUS="invalid_passphrase_length"
        PUBLIC_AP_ENV_ERROR="WPA_PASSPHRASE must be 8-63 characters long; got $pass_length."
        return 1
    fi

    PUBLIC_AP_ENV_STATUS="ok"
    PUBLIC_AP_ENV_ERROR=""
}

public_ap_wifi_interfaces() {
    local iface_path iface
    for iface_path in /sys/class/net/*; do
        [[ -e $iface_path ]] || continue
        iface=$(basename "$iface_path")
        [[ $iface == lo ]] && continue
        if [[ -d $iface_path/wireless ]]; then
            printf '%s\n' "$iface"
            continue
        fi
        if grep -q "^DEVTYPE=wlan$" "$iface_path/uevent" 2>/dev/null; then
            printf '%s\n' "$iface"
        fi
    done
}

public_ap_iface_phy() {
    local iface=$1
    local wiphy
    wiphy=$(iw dev "$iface" info 2>/dev/null | awk '/wiphy/ {print "phy"$2; exit}')
    [[ -n $wiphy ]] || return 1
    printf '%s\n' "$wiphy"
}

public_ap_iface_supports_ap() {
    local iface=$1
    local phy

    # If iw is missing, do not block startup. hostapd will return the real error.
    command -v iw >/dev/null 2>&1 || return 0
    phy=$(public_ap_iface_phy "$iface") || return 1
    iw phy "$phy" info 2>/dev/null | awk '
        $1 == "*" && $2 == "AP" { found=1 }
        END { exit found ? 0 : 1 }
    '
}

public_ap_detect_wifi_iface() {
    local iface first_iface=""

    if [[ -n ${WIFI_IFACE:-} ]] && ip link show "$WIFI_IFACE" >/dev/null 2>&1; then
        printf '%s\n' "$WIFI_IFACE"
        return 0
    fi

    while IFS= read -r iface; do
        [[ -n $iface ]] || continue
        [[ -n $first_iface ]] || first_iface=$iface
        if public_ap_iface_supports_ap "$iface"; then
            printf '%s\n' "$iface"
            return 0
        fi
    done < <(public_ap_wifi_interfaces)

    if [[ -n $first_iface ]]; then
        printf '%s\n' "$first_iface"
        return 0
    fi
    return 1
}

public_ap_log_diagnostics() {
    public_ap_log "=== RiddleMatrix hotspot diagnostics ==="
    public_ap_log "Kernel: $(uname -a 2>/dev/null || true)"
    public_ap_log "--- rfkill ---"
    rfkill list 2>/dev/null || true
    public_ap_log "--- ip link ---"
    ip link 2>/dev/null || true
    public_ap_log "--- iw dev ---"
    iw dev 2>/dev/null || true
    public_ap_log "--- AP-capable WiFi interfaces ---"

    local iface
    while IFS= read -r iface; do
        [[ -n $iface ]] || continue
        if public_ap_iface_supports_ap "$iface"; then
            public_ap_log "$iface: AP mode supported"
        else
            public_ap_log "$iface: AP mode not reported"
        fi
    done < <(public_ap_wifi_interfaces)

    public_ap_log "--- USB devices ---"
    lsusb 2>/dev/null || true
    public_ap_log "--- PCI devices ---"
    lspci -nn 2>/dev/null || true
    public_ap_log "--- recent firmware/kernel messages ---"
    dmesg 2>/dev/null | grep -Ei 'firmware|wifi|wlan|iwlwifi|rtw|rtl|ath|brcm|mediatek|mt76|hostapd' | tail -n 120 || true
}

public_ap_escape_sed() {
    printf '%s' "$1" | sed -e 's/[\\&|]/\\&/g'
}

public_ap_render_template() {
    local template=$1 output=$2
    shift 2
    if [[ ! -f $template ]]; then
        public_ap_log "Template $template was not found."
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
        public_ap_log "Hostapd template $template was not found."
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
# DHCP configuration for hotspot "$SSID"
interface=$wifi_iface
bind-interfaces
dhcp-range=192.168.10.100,192.168.10.200,12h
EOF
}
