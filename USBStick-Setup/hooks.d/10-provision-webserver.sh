#!/bin/bash
set -euo pipefail

VENV_DIR=""
VENV_TARGET_PATH=""

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

run_in_target() {
  if [[ "$TARGET_ROOT" = "/" ]]; then
    "$@"
    return
  fi

  if ! command -v chroot >/dev/null 2>&1; then
    die "chroot not available; cannot execute commands inside target root $TARGET_ROOT"
  fi

  chroot "$TARGET_ROOT" /usr/bin/env \
    PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
    "$@"
}

ensure_virtualenv() {
  local target_python=""

  if [[ -d "$VENV_DIR/bin" ]]; then
    log "Python virtual environment already present at $VENV_DIR"
  else
    log "Creating Python virtual environment at $VENV_DIR"
    mkdir -p "$VENV_DIR"
    run_in_target python3 -m venv "$VENV_TARGET_PATH"
  fi

  if [[ -x "$VENV_DIR/bin/python3" ]]; then
    target_python="$VENV_TARGET_PATH/bin/python3"
  elif [[ -x "$VENV_DIR/bin/python" ]]; then
    target_python="$VENV_TARGET_PATH/bin/python"
  else
    die "Virtual environment at $VENV_DIR is incomplete"
  fi

  if [[ ! -x "$VENV_DIR/bin/pip" ]]; then
    log "Ensuring pip is available inside the virtual environment"
    run_in_target "$target_python" -m ensurepip --upgrade
  fi
}

install_python_dependencies() {
  local -a dependencies=(flask requests beautifulsoup4)
  local -a missing=()
  local target_python=""

  if [[ -x "$VENV_DIR/bin/python3" ]]; then
    target_python="$VENV_TARGET_PATH/bin/python3"
  elif [[ -x "$VENV_DIR/bin/python" ]]; then
    target_python="$VENV_TARGET_PATH/bin/python"
  else
    die "Virtual environment at $VENV_DIR is missing a python interpreter"
  fi

  for dep in "${dependencies[@]}"; do
    if ! run_in_target "$target_python" -m pip show "$dep" >/dev/null 2>&1; then
      missing+=("$dep")
    fi
  done

  if [[ ${#missing[@]} -eq 0 ]]; then
    log "Python dependencies already installed inside virtual environment"
    return
  fi

  log "Installing Python dependencies: ${missing[*]}"
  run_in_target "$target_python" -m pip install --disable-pip-version-check --no-input "${missing[@]}"
}

main() {
  resolve_target_root "$1"
  VENV_TARGET_PATH="/usr/local/venv/maerchen"
  if [[ "$TARGET_ROOT" = "/" ]]; then
    VENV_DIR="$VENV_TARGET_PATH"
  else
    VENV_DIR="$TARGET_ROOT$VENV_TARGET_PATH"
  fi
  ensure_system_packages
  ensure_virtualenv
  install_python_dependencies
  log "Provisioning hook completed"
}

main "${1:-}"
