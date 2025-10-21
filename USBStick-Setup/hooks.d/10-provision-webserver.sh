#!/bin/bash
set -euo pipefail

VENV_DIR=""

log() {
  printf '[provision] %s\n' "$1"
}

warn() {
  printf '[provision] ⚠️  %s\n' "$1" >&2
}

die() {
  printf '[provision] ❌ %s\n' "$1" >&2
  exit 1
}

resolve_target_root() {
  local arg="${1:-}" env_root="${TARGET_ROOT:-}"
  if [[ -n "$arg" ]]; then
    TARGET_ROOT="$arg"
  elif [[ -n "$env_root" ]]; then
    TARGET_ROOT="$env_root"
  else
    TARGET_ROOT="/"
  fi

  if [[ -z "$TARGET_ROOT" ]]; then
    TARGET_ROOT="/"
  fi

  if [[ "$TARGET_ROOT" != "/" ]]; then
    TARGET_ROOT="${TARGET_ROOT%/}"
    [[ -n "$TARGET_ROOT" ]] || TARGET_ROOT="/"
  fi

  if [[ ! -d "$TARGET_ROOT" ]]; then
    die "Target root $TARGET_ROOT does not exist"
  fi
}

ensure_system_packages() {
  local -a packages=(
    python3
    python3-venv
    python3-pip
    dnsmasq
    hostapd
    rfkill
    curl
    wget
    xserver-xorg
    xinit
    x11-xserver-utils
    firefox-esr
    firmware-linux-nonfree
    firmware-iwlwifi
    firmware-realtek
    firmware-misc-nonfree
    libffi-dev
    libssl-dev
    build-essential
    linux-image-cloud-amd64
  )

  local admindir="$TARGET_ROOT/var/lib/dpkg"
  local dpkg_query="dpkg-query"
  if ! command -v "$dpkg_query" >/dev/null 2>&1; then
    warn "dpkg-query not available; skipping package checks"
    return
  fi

  if [[ ! -d "$admindir" ]]; then
    if [[ "$TARGET_ROOT" = "/" ]]; then
      admindir="/var/lib/dpkg"
    else
      warn "Package database $admindir missing; skipping package checks"
      return
    fi
  fi

  local -a missing=()
  local pkg status
  for pkg in "${packages[@]}"; do
    if ! status=$("$dpkg_query" --admindir="$admindir" -W -f='${Status}' "$pkg" 2>/dev/null); then
      missing+=("$pkg")
      continue
    fi
    if [[ "$status" != *"install ok installed"* ]]; then
      missing+=("$pkg")
    fi
  done

  if [[ ${#missing[@]} -eq 0 ]]; then
    log "All required APT packages are already installed"
    return
  fi

  local apt_cmd
  if command -v apt-get >/dev/null 2>&1; then
    apt_cmd="apt-get"
  elif command -v apt >/dev/null 2>&1; then
    apt_cmd="apt"
  else
    warn "Neither apt-get nor apt available; cannot install packages"
    return
  fi

  if [[ "$TARGET_ROOT" = "/" ]]; then
    log "Installing missing packages: ${missing[*]}"
    "$apt_cmd" update
    "$apt_cmd" install -y "${missing[@]}"
    return
  fi

  if ! command -v chroot >/dev/null 2>&1; then
    warn "chroot not available; cannot install packages for target $TARGET_ROOT"
    return
  fi

  log "Installing missing packages inside chroot $TARGET_ROOT: ${missing[*]}"
  chroot "$TARGET_ROOT" "$apt_cmd" update
  chroot "$TARGET_ROOT" "$apt_cmd" install -y "${missing[@]}"
}

venv_python_bin() {
  if [[ -n "$VENV_DIR" && -x "$VENV_DIR/bin/python3" ]]; then
    printf '%s\n' "$VENV_DIR/bin/python3"
    return 0
  fi
  if [[ -n "$VENV_DIR" && -x "$VENV_DIR/bin/python" ]]; then
    printf '%s\n' "$VENV_DIR/bin/python"
    return 0
  fi
  return 1
}

ensure_virtualenv() {
  local host_python
  if command -v python3 >/dev/null 2>&1; then
    host_python="$(command -v python3)"
  else
    die "python3 binary not found on the host system"
  fi

  if [[ -d "$VENV_DIR/bin" ]]; then
    log "Python virtual environment already present at $VENV_DIR"
  else
    log "Creating Python virtual environment at $VENV_DIR"
    mkdir -p "$VENV_DIR"
    "$host_python" -m venv "$VENV_DIR"
  fi

  local venv_python
  if ! venv_python="$(venv_python_bin)"; then
    die "Virtual environment at $VENV_DIR is incomplete"
  fi

  if [[ ! -x "$VENV_DIR/bin/pip" ]]; then
    log "Ensuring pip is available inside the virtual environment"
    "$venv_python" -m ensurepip --upgrade
  fi
}

install_python_dependencies() {
  local -a dependencies=(flask requests beautifulsoup4)
  local -a missing=()
  local venv_python

  if ! venv_python="$(venv_python_bin)"; then
    die "Virtual environment at $VENV_DIR is missing a python interpreter"
  fi

  for dep in "${dependencies[@]}"; do
    if ! "$venv_python" -m pip show "$dep" >/dev/null 2>&1; then
      missing+=("$dep")
    fi
  done

  if [[ ${#missing[@]} -eq 0 ]]; then
    log "Python dependencies already installed inside virtual environment"
    return
  fi

  log "Installing Python dependencies: ${missing[*]}"
  "$venv_python" -m pip install --disable-pip-version-check --no-input "${missing[@]}"
}

main() {
  resolve_target_root "$1"
  VENV_DIR="$TARGET_ROOT/usr/local/venv/maerchen"
  ensure_system_packages
  ensure_virtualenv
  install_python_dependencies
  log "Provisioning hook completed"
}

main "${1:-}"
