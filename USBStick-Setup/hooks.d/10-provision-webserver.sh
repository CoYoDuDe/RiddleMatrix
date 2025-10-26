#!/bin/bash
set -euo pipefail

VENV_DIR=""
DRY_RUN="${PROVISION_DRY_RUN:-0}"
USE_CHROOT=0
PYTHON_STEPS_ENABLED=1

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

is_dry_run() {
  [[ "$DRY_RUN" = "1" ]]
}

format_cmd() {
  local out="" arg
  for arg in "$@"; do
    if [[ -z "$out" ]]; then
      out="$(printf '%q' "$arg")"
    else
      out+=" $(printf '%q' "$arg")"
    fi
  done
  printf '%s' "$out"
}

path_inside_target() {
  local abs="$1"
  if [[ "$TARGET_ROOT" = "/" ]]; then
    printf '%s\n' "$abs"
    return
  fi

  case "$abs" in
    "$TARGET_ROOT")
      printf '/\n'
      ;;
    "$TARGET_ROOT"/*)
      printf '/%s\n' "${abs#"$TARGET_ROOT/"}"
      ;;
    *)
      printf '%s\n' "$abs"
      ;;
  esac
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

log_dry_run_banner() {
  if is_dry_run; then
    log "Dry-run aktiviert – es werden keine Änderungen am Dateisystem vorgenommen."
  fi
}

ensure_system_packages() {
  local target_arch=""

  if [[ "$TARGET_ROOT" = "/" ]]; then
    if command -v dpkg >/dev/null 2>&1; then
      target_arch="$(dpkg --print-architecture 2>/dev/null || true)"
    else
      warn "dpkg nicht gefunden – Zielarchitektur kann nicht bestimmt werden"
    fi
  else
    if [[ -x "$TARGET_ROOT/usr/bin/dpkg" ]]; then
      if command -v chroot >/dev/null 2>&1; then
        target_arch="$(chroot "$TARGET_ROOT" dpkg --print-architecture 2>/dev/null || true)"
      else
        warn "chroot nicht verfügbar – kann dpkg im Zielsystem nicht ausführen"
      fi
    else
      warn "dpkg im Zielsystem nicht verfügbar – Zielarchitektur unbekannt"
    fi
  fi

  if [[ -z "$target_arch" ]] && command -v dpkg >/dev/null 2>&1; then
    warn "Verwende Host-Architektur als Fallback"
    target_arch="$(dpkg --print-architecture 2>/dev/null || true)"
  fi

  if [[ -z "$target_arch" ]]; then
    warn "Zielarchitektur konnte nicht ermittelt werden"
  fi

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
  )

  if [[ "$target_arch" = "amd64" ]]; then
    packages+=(linux-image-cloud-amd64)
  fi

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
    if is_dry_run; then
      log "DRY-RUN: würde fehlende Pakete installieren: ${missing[*]}"
      return
    fi
    log "Installing missing packages: ${missing[*]}"
    "$apt_cmd" update
    "$apt_cmd" install -y "${missing[@]}"
    return
  fi

  if ! command -v chroot >/dev/null 2>&1; then
    warn "chroot not available; cannot install packages for target $TARGET_ROOT"
    return
  fi

  if is_dry_run; then
    log "DRY-RUN: würde chroot $TARGET_ROOT $(format_cmd "$apt_cmd" update)"
    log "DRY-RUN: würde chroot $TARGET_ROOT $(format_cmd "$apt_cmd" install -y "${missing[@]}")"
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
  if is_dry_run; then
    if [[ "$USE_CHROOT" -eq 1 ]]; then
      log "DRY-RUN: würde virtuelle Umgebung via chroot unter $(path_inside_target "$VENV_DIR") anlegen."
    else
      log "DRY-RUN: würde virtuelle Umgebung auf dem Host unter $VENV_DIR anlegen."
    fi
    PYTHON_STEPS_ENABLED=0
    return
  fi

  if [[ "$PYTHON_STEPS_ENABLED" -ne 1 ]]; then
    return
  fi

  local host_python
  if command -v python3 >/dev/null 2>&1; then
    host_python="$(command -v python3)"
  else
    die "python3 binary not found on the host system"
  fi

  if [[ "$USE_CHROOT" -eq 0 ]]; then
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
    return
  fi

  if ! command -v chroot >/dev/null 2>&1; then
    warn "chroot ist nicht verfügbar – überspringe Python-Provisionierung für $TARGET_ROOT"
    PYTHON_STEPS_ENABLED=0
    return
  fi

  local target_python="$TARGET_ROOT/usr/bin/python3"
  if [[ ! -x "$target_python" ]]; then
    warn "Target python3 binary missing at $target_python – überspringe Provisionierung"
    PYTHON_STEPS_ENABLED=0
    return
  fi

  local venv_inside
  venv_inside="$(path_inside_target "$VENV_DIR")"

  if [[ -d "$VENV_DIR/bin" ]]; then
    log "Python virtual environment already present at $VENV_DIR"
  else
    log "Creating Python virtual environment via chroot at $venv_inside"
    mkdir -p "$VENV_DIR"
    chroot "$TARGET_ROOT" /usr/bin/python3 -m venv "$venv_inside"
  fi

  local venv_python
  if ! venv_python="$(venv_python_bin)"; then
    die "Virtual environment at $VENV_DIR is incomplete"
  fi

  if [[ ! -x "$VENV_DIR/bin/pip" ]]; then
    log "Ensuring pip is available inside the virtual environment via chroot"
    chroot "$TARGET_ROOT" "$(path_inside_target "$venv_python")" -m ensurepip --upgrade
  fi
}

install_python_dependencies() {
  local -a dependencies=(flask requests beautifulsoup4)
  local -a missing=()
  local venv_python

  if is_dry_run; then
    if [[ "$USE_CHROOT" -eq 1 ]]; then
      log "DRY-RUN: würde Python-Abhängigkeiten im Zielsystem via chroot installieren"
    else
      log "DRY-RUN: würde Python-Abhängigkeiten im virtuellen Host-Environment installieren"
    fi
    return
  fi

  if [[ "$PYTHON_STEPS_ENABLED" -ne 1 ]]; then
    warn "Skipping Python dependency installation because the virtual environment is unavailable"
    return
  fi

  if ! venv_python="$(venv_python_bin)"; then
    die "Virtual environment at $VENV_DIR is missing a python interpreter"
  fi

  if [[ "$USE_CHROOT" -eq 1 ]]; then
    local venv_python_inside
    venv_python_inside="$(path_inside_target "$venv_python")"
    for dep in "${dependencies[@]}"; do
      if ! chroot "$TARGET_ROOT" "$venv_python_inside" -m pip show "$dep" >/dev/null 2>&1; then
        missing+=("$dep")
      fi
    done

    if [[ ${#missing[@]} -eq 0 ]]; then
      log "Python dependencies already installed inside virtual environment"
      return
    fi

    log "Installing Python dependencies inside chroot $TARGET_ROOT: ${missing[*]}"
    chroot "$TARGET_ROOT" "$venv_python_inside" -m pip install --disable-pip-version-check --no-input "${missing[@]}"
    return
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
  if [[ "$TARGET_ROOT" != "/" ]]; then
    USE_CHROOT=1
  fi
  log_dry_run_banner
  VENV_DIR="$TARGET_ROOT/usr/local/venv/maerchen"
  ensure_system_packages
  ensure_virtualenv
  install_python_dependencies
  log "Provisioning hook completed"
}

main "${1:-}"
