#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FILES_DIR="$SCRIPT_DIR/files"
HOOKS_DIR="$SCRIPT_DIR/hooks.d"
TARGET_ROOT="/"
DRY_RUN=0
SKIP_SYSTEMD=0
SKIP_HOOKS=0
PUBLIC_AP_ENV_TEMPLATE="etc/usbstick/public_ap.env.example"
PUBLIC_AP_ENV_FILE="etc/usbstick/public_ap.env"

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
  local -a excluded_exact_paths=(
    "$PUBLIC_AP_ENV_FILE"
    "etc/usbstick/public_ap.env.local"
  )
  local -a excluded_pattern_paths=(
    ".gitkeep"
    "etc/usbstick/public_ap.env.d/*"
  )

  for rel in "${excluded_exact_paths[@]}"; do
    log "Bestehende Hotspot-Konfiguration $TARGET_ROOT/$rel bleibt unverändert (vom Kopiervorgang ausgeschlossen)"
  done

  if command -v rsync >/dev/null 2>&1; then
    local -a rsync_args=(-a)
    ((DRY_RUN)) && rsync_args+=(-n -v)
    for pattern in "${excluded_pattern_paths[@]}"; do
      rsync_args+=(--exclude="$pattern")
    done
    for rel in "${excluded_exact_paths[@]}"; do
      rsync_args+=(--exclude="$rel")
    done
    rsync "${rsync_args[@]}" "$FILES_DIR"/ "$TARGET_ROOT"/
  else
    warn "rsync not available; falling back to tar for deployment"
    local -a tar_exclude_display=()
    local -a tar_exclude_args=()
    for pattern in "${excluded_pattern_paths[@]}"; do
      tar_exclude_display+=("$pattern")
      tar_exclude_args+=("--exclude=$pattern" "--exclude=./$pattern")
      if [[ "$pattern" != */* ]]; then
        tar_exclude_args+=("--exclude=*/$pattern")
      fi
    done
    for rel in "${excluded_exact_paths[@]}"; do
      tar_exclude_display+=("$rel")
      tar_exclude_args+=("--exclude=$rel" "--exclude=./$rel")
    done

    if ((DRY_RUN)); then
      (cd "$FILES_DIR" && find . -mindepth 1 -print | sed 's#^./##' | while read -r path; do
        local skip=0
        for pattern in "${excluded_pattern_paths[@]}"; do
          if [[ "$path" == $pattern ]]; then
            skip=1
            break
          fi
          if [[ "$pattern" != */* && "$path" == */$pattern ]]; then
            skip=1
            break
          fi
        done
        if ((skip == 0)); then
          for rel in "${excluded_exact_paths[@]}"; do
            if [[ "$path" == "$rel" ]]; then
              skip=1
              break
            fi
          done
        fi
        if ((skip)); then
          log "[dry-run] überspringe ausgeschlossene Datei $path"
          continue
        fi
        log "[dry-run] copy $path"
      done)
    else
      if ((${#tar_exclude_display[@]})); then
        local joined=""
        printf -v joined '%s, ' "${tar_exclude_display[@]}"
        joined=${joined%%, }
        log "Tar-Fallback überspringt kundenspezifische Dateien: $joined"
      fi
      local -a tar_args=(cf -)
      tar_args+=("${tar_exclude_args[@]}")
      (cd "$FILES_DIR" && tar "${tar_args[@]}" .) | (cd "$TARGET_ROOT" && tar xpf -)
    fi
  fi
}

deploy_public_ap_env() {
  local template_source="$FILES_DIR/$PUBLIC_AP_ENV_TEMPLATE"
  local target_file="$TARGET_ROOT/$PUBLIC_AP_ENV_FILE"

  [[ -f "$template_source" ]] || return

  if ((DRY_RUN)); then
    if [[ -f "$target_file" ]]; then
      log "[dry-run] Hotspot-Umgebung $target_file existiert bereits und bleibt bestehen"
    else
      log "[dry-run] install -m 0640 $template_source $target_file"
    fi
    return
  fi

  mkdir -p "$(dirname "$target_file")"
  if [[ -f "$target_file" ]]; then
    log "Hotspot-Umgebungsdatei $target_file existiert bereits; überspringe Kopie aus der Vorlage"
    return
  fi

  install -m 0640 "$template_source" "$target_file"
  log "Hotspot-Umgebung aus Vorlage nach $target_file kopiert"
}

ensure_directories() {
  local -a dirs=(
    "$TARGET_ROOT/var/lib/misc"
    "$TARGET_ROOT/home/kioskuser"
    "$TARGET_ROOT/usr/local/bin"
    "$TARGET_ROOT/usr/local/libexec"
    "$TARGET_ROOT/etc/systemd/system"
    "$TARGET_ROOT/etc/dnsmasq.d"
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

ensure_kioskuser() {
  local user="kioskuser"
  local shell="/bin/bash"
  local home_dir="/home/$user"
  local -a useradd_opts=(-r -s "$shell" -d "$home_dir")

  if [[ "$TARGET_ROOT" != "/" ]]; then
    log "Benutzerprüfung für '$user' wird für Ziel $TARGET_ROOT übersprungen; Benutzer bitte im Zielsystem anlegen"
    return 0
  fi

  if id "$user" >/dev/null 2>&1; then
    log "Benutzer '$user' ist bereits vorhanden"
    return 0
  fi

  if [[ -d "$home_dir" ]]; then
    log "Home-Verzeichnis $home_dir existiert bereits; useradd wird ohne -m ausgeführt"
    useradd_opts+=(-M)
  else
    useradd_opts+=(-m)
  fi

  if ((DRY_RUN)); then
    log "[dry-run] useradd ${useradd_opts[*]} $user"
    log "[dry-run] Benutzer '$user' würde angelegt"
    return 0
  fi

  if run_cmd useradd "${useradd_opts[@]}" "$user"; then
    log "Benutzer '$user' wurde erfolgreich angelegt"
    return 0
  fi

  warn "Benutzer '$user' konnte nicht angelegt werden; bitte Systemprotokolle prüfen"
  return 1
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
    "etc/dnsmasq.d/riddlematrix-hotspot.conf"
  )
  for rel in "${config_paths[@]}"; do
    local target="$TARGET_ROOT/$rel"
    if [[ -e "$target" ]]; then
      run_cmd chmod 0644 "$target"
    else
      warn "Konfigurationsdatei $rel fehlt; überspringe chmod"
    fi
  done

  local public_ap_env="$TARGET_ROOT/$PUBLIC_AP_ENV_FILE"
  if [[ -e "$public_ap_env" ]]; then
    run_cmd chmod 0640 "$public_ap_env"
  elif ((DRY_RUN)); then
    log "[dry-run] Hotspot-Umgebungsdatei $public_ap_env würde erzeugt"
  else
    warn "Hotspot-Umgebungsdatei $PUBLIC_AP_ENV_FILE wurde nicht gefunden"
  fi

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
    if ensure_kioskuser; then
      if [[ -d "$TARGET_ROOT/home/kioskuser" ]]; then
        run_cmd chown -R kioskuser:kioskuser "$TARGET_ROOT/home/kioskuser"
      fi
    else
      warn "Benutzer 'kioskuser' fehlt weiterhin; Besitzanpassungen werden übersprungen"
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
    if ensure_kioskuser; then
      run_cmd systemctl restart "$kiosk_unit"
    else
      warn "Überspringe Neustart von $kiosk_unit, da 'kioskuser' nicht angelegt werden konnte"
    fi
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
  deploy_public_ap_env
  set_permissions
  enable_systemd_units
  run_post_install_hooks
  log "Deployment completed"
}

main "$@"
