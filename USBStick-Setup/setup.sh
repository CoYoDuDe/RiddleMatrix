#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FILES_DIR="$SCRIPT_DIR/files"
HOOKS_DIR="$SCRIPT_DIR/hooks.d"
TARGET_ROOT="/"
DRY_RUN=0
SKIP_SYSTEMD=0
SKIP_HOOKS=0

log() {
  printf '[%s] %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$1"
}

warn() {
  printf '[%s] ⚠️  %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$1" >&2
}

die() {
  printf '[%s] ❌ %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$1" >&2
  exit 1
}

usage() {
  cat <<'USAGE'
Usage: setup.sh [options]

Deploy the prepared USB stick payload from the local files/ directory onto the
current system (or a mounted target root).

Options:
  -t, --target <path>   Absolute path to the target root (default: /)
  -n, --dry-run         Show the planned operations without modifying the system
      --skip-systemd    Do not enable or reload systemd units after deployment
      --skip-hooks      Skip execution of post-install hooks in hooks.d/
  -h, --help            Show this help message

Examples:
  sudo ./setup.sh
  sudo ./setup.sh --target /mnt/venus-os
  sudo ./setup.sh --dry-run
USAGE
}

run_cmd() {
  if ((DRY_RUN)); then
    log "[dry-run] $*"
  else
    "$@"
  fi
}

parse_args() {
  while [[ $# -gt 0 ]]; do
    case "$1" in
      -t|--target)
        shift || die "Missing argument for --target"
        TARGET_ROOT="$1"
        ;;
      -n|--dry-run)
        DRY_RUN=1
        ;;
      --skip-systemd)
        SKIP_SYSTEMD=1
        ;;
      --skip-hooks)
        SKIP_HOOKS=1
        ;;
      -h|--help)
        usage
        exit 0
        ;;
      *)
        die "Unknown option: $1"
        ;;
    esac
    shift || break
  done
}

validate_environment() {
  [[ -d "$FILES_DIR" ]] || die "Payload directory $FILES_DIR not found"

  if [[ "$TARGET_ROOT" != /* ]]; then
    die "Target root must be an absolute path"
  fi

  TARGET_ROOT="${TARGET_ROOT%/}"
  [[ -n "$TARGET_ROOT" ]] || TARGET_ROOT="/"

  if [[ "$TARGET_ROOT" = "/" && $DRY_RUN -eq 0 && $(id -u) -ne 0 ]]; then
    die "Root privileges are required to deploy to /"
  fi

  if ((DRY_RUN == 0)); then
    run_cmd mkdir -p "$TARGET_ROOT"
  else
    log "Dry-run enabled; no changes will be written to $TARGET_ROOT"
  fi
}

copy_payload() {
  log "Deploying files from $FILES_DIR to $TARGET_ROOT"
  if command -v rsync >/dev/null 2>&1; then
    local -a rsync_args=(-a)
    ((DRY_RUN)) && rsync_args+=(-n -v)
    rsync_args+=(--exclude='.gitkeep')
    rsync "${rsync_args[@]}" "$FILES_DIR"/ "$TARGET_ROOT"/
  else
    warn "rsync not available; falling back to tar for deployment"
    if ((DRY_RUN)); then
      (cd "$FILES_DIR" && find . -mindepth 1 -print | sed 's#^./##' | while read -r path; do
        log "[dry-run] copy $path"
      done)
    else
      (cd "$FILES_DIR" && tar cf - .) | (cd "$TARGET_ROOT" && tar xpf -)
    fi
  fi
}

ensure_directories() {
  local -a dirs=(
    "$TARGET_ROOT/var/lib/misc"
    "$TARGET_ROOT/home/kioskuser"
    "$TARGET_ROOT/usr/local/bin"
    "$TARGET_ROOT/usr/local/libexec"
    "$TARGET_ROOT/etc/systemd/system"
    "$TARGET_ROOT/etc/usbstick"
  )
  for dir in "${dirs[@]}"; do
    if ((DRY_RUN)); then
      [[ -d "$dir" ]] || log "[dry-run] mkdir -p $dir"
    else
      mkdir -p "$dir"
    fi
  done
}

set_permissions() {
  local -a exec_paths=(
    "usr/local/bin/bootlocal.sh"
    "usr/local/bin/webserver.py"
    "home/kioskuser/start_firefox.sh"
    "etc/hostapd/ifupdown.sh"
    "root/install_public_ap.sh"
    "root/install_public_ap_setuphelper.sh"
  )
  for rel in "${exec_paths[@]}"; do
    local target="$TARGET_ROOT/$rel"
    if [[ -e "$target" ]]; then
      run_cmd chmod 0755 "$target"
    else
      warn "Missing executable $rel; skipping chmod"
    fi
  done

  local -a config_paths=(
    "home/kioskuser/.xinitrc"
  )
  for rel in "${config_paths[@]}"; do
    local target="$TARGET_ROOT/$rel"
    if [[ -e "$target" ]]; then
      run_cmd chmod 0644 "$target"
    else
      warn "Missing config $rel; skipping chmod"
    fi
  done

  local leases="$TARGET_ROOT/var/lib/misc/dnsmasq.leases"
  local owner_user="root"
  local owner_group="dnsmasq"
  local owner_spec="$owner_user:$owner_group"
  local group_exists=0
  if getent group "$owner_group" >/dev/null 2>&1; then
    group_exists=1
  else
    warn "Gruppe '$owner_group' nicht gefunden; dnsmasq muss installiert sein, damit $leases beschreibbar bleibt"
  fi

  if [[ -e "$leases" ]]; then
    run_cmd chmod 0640 "$leases"
  else
    if ((group_exists)); then
      run_cmd install -o "$owner_user" -g "$owner_group" -m 0640 /dev/null "$leases"
    else
      run_cmd install -o "$owner_user" -g "$owner_user" -m 0640 /dev/null "$leases"
    fi
  fi

  if ((group_exists)); then
    run_cmd chown "$owner_spec" "$leases"
  fi

  if [[ "$TARGET_ROOT" = "/" ]]; then
    if id kioskuser >/dev/null 2>&1; then
      if [[ -d "$TARGET_ROOT/home/kioskuser" ]]; then
        run_cmd chown -R kioskuser:kioskuser "$TARGET_ROOT/home/kioskuser"
      fi
    else
      warn "User 'kioskuser' not present; skipping ownership adjustments"
    fi
  else
    warn "Skipping ownership updates for $TARGET_ROOT; adjust after mounting"
  fi
}

enable_systemd_units() {
  ((SKIP_SYSTEMD)) && { log "Skipping systemd integration as requested"; return; }
  [[ "$TARGET_ROOT" = "/" ]] || { warn "Systemd enable skipped (target root is $TARGET_ROOT)"; return; }
  command -v systemctl >/dev/null 2>&1 || { warn "systemctl not available; skipping service enable"; return; }

  local -a units=(bootlocal.service webserver.service kiosk-startx.service)
  run_cmd systemctl daemon-reload
  for unit in "${units[@]}"; do
    if [[ -f "$TARGET_ROOT/etc/systemd/system/$unit" ]]; then
      run_cmd systemctl enable "$unit"
    else
      warn "Service file $unit missing; cannot enable"
    fi
  done

  local kiosk_unit="kiosk-startx.service"
  if [[ -f "$TARGET_ROOT/etc/systemd/system/$kiosk_unit" ]]; then
    run_cmd systemctl restart "$kiosk_unit"
  else
    warn "Service file $kiosk_unit missing; cannot restart"
  fi
}

run_post_install_hooks() {
  ((SKIP_HOOKS)) && { log "Skipping hooks as requested"; return; }
  [[ -d "$HOOKS_DIR" ]] || { log "No hooks directory found; skipping"; return; }

  mapfile -t hooks < <(find "$HOOKS_DIR" -maxdepth 1 -type f -name '*.sh' -print | sort)
  if [[ ${#hooks[@]} -eq 0 ]]; then
    log "No post-install hooks detected"
    return
  fi

  for hook in "${hooks[@]}"; do
    if [[ ! -x "$hook" ]]; then
      warn "Hook $hook is not executable; skipping"
      continue
    fi
    if ((DRY_RUN)); then
      log "[dry-run] execute hook $hook"
    else
      log "Running hook $hook"
      TARGET_ROOT="$TARGET_ROOT" "$hook" "$TARGET_ROOT"
    fi
  done
}

main() {
  parse_args "$@"
  validate_environment
  ensure_directories
  copy_payload
  set_permissions
  enable_systemd_units
  run_post_install_hooks
  log "Deployment completed"
}

main "$@"
