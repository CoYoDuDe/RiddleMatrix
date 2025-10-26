#!/usr/bin/env python3
from flask import Flask, abort, jsonify, request, send_from_directory
from bs4 import BeautifulSoup
import math
import os, json, subprocess, requests
import tempfile
import secrets
import re
import ipaddress
import hmac
from typing import Optional
from functools import lru_cache

DAYS = ["mo", "di", "mi", "do", "fr", "sa", "so"]
DAY_TO_FIRMWARE_INDEX = {
    "so": 0,
    "mo": 1,
    "di": 2,
    "mi": 3,
    "do": 4,
    "fr": 5,
    "sa": 6,
}
TRIGGER_SLOTS = 3
DEFAULT_COLOR = "#ffffff"
DEFAULT_DELAY = 0

_HEX_COLOR_PATTERN = re.compile(r"^#[0-9A-Fa-f]{6}$")

_ALLOWED_HOSTNAME_PATTERN = re.compile(r"[^A-Za-z0-9._-]+")

SAFE_IP_PLACEHOLDER = "0.0.0.0"

_FIRMWARE_LETTERS = tuple("ABCDEFGHIJKLMNOPQRSTUVWXYZ*#~&?")
_ALLOWED_LETTERS = set(_FIRMWARE_LETTERS)

app = Flask(__name__)
LEASE_FILE = "/var/lib/misc/dnsmasq.leases"
CONFIG_FILE = "/mnt/persist/boxen_config/boxen_config.json"
SHUTDOWN_TOKEN_ENV_VAR = "SHUTDOWN_TOKEN"
SHUTDOWN_TOKEN_HEADER = "X-Setup-Token"
PUBLIC_AP_ENV_FILE = os.environ.get("PUBLIC_AP_ENV_FILE", "/etc/usbstick/public_ap.env")
SHUTDOWN_COMMAND_ENV_VAR = "SHUTDOWN_COMMAND"
_LOCALHOST_ADDRESSES = {"127.0.0.1", "::1"}

_public_ap_env_cache = {"path": None, "mtime": None, "data": {}}


class RedirectResponseError(RuntimeError):
    def __init__(self, action: str, host: str, status_code: int) -> None:
        super().__init__(f"{action} ({host})")
        self.action = action
        self.host = host
        self.status_code = status_code


def _ensure_no_redirect(response, *, action: str, host: str) -> None:
    status_code = getattr(response, "status_code", 0)
    if (
        getattr(response, "is_redirect", False)
        or getattr(response, "is_permanent_redirect", False)
        or 300 <= status_code < 400
    ):
        app.logger.warning(
            "%s für %s abgebrochen: unerwartete Weiterleitung (HTTP %s)",
            action,
            host,
            status_code,
        )
        raise RedirectResponseError(action, host, status_code)


def _redirect_error_response(error: RedirectResponseError):
    return (
        jsonify(
            {
                "status": "❌ Unerwartete Weiterleitung",
                "details": f"{error.action} ({error.host}) lieferte HTTP {error.status_code} mit Weiterleitung",
            }
        ),
        502,
    )

def _default_config():
    return {"boxen": {}, "boxOrder": []}

def load_config():
    changed = False
    try:
        with open(CONFIG_FILE, "r") as f:
            data = json.load(f)
        if not isinstance(data, dict):
            app.logger.warning(
                "Konfigurationsdatei %s enthält unerwarteten Typ (%s) – Standardwerte werden verwendet",
                CONFIG_FILE,
                type(data).__name__,
            )
            data = _default_config()
            changed = True
    except (json.JSONDecodeError, OSError) as exc:
        app.logger.warning(
            "Konfigurationsdatei %s defekt – Standardwerte werden verwendet: %s",
            CONFIG_FILE,
            exc,
        )
        data = _default_config()
        save_config(data)

    if migrate_config(data):
        changed = True
    if _normalize_config_ips(data):
        changed = True

    if changed:
        save_config(data)

    return data

def save_config(data):
    directory = os.path.dirname(CONFIG_FILE)
    os.makedirs(directory, exist_ok=True)

    fd, temp_path = tempfile.mkstemp(dir=directory, prefix=".config-", suffix=".tmp")
    try:
        with os.fdopen(fd, "w") as tmp_file:
            json.dump(data, tmp_file, indent=4)
            tmp_file.flush()
            os.fsync(tmp_file.fileno())

        os.replace(temp_path, CONFIG_FILE)
    except Exception:
        try:
            os.unlink(temp_path)
        except OSError:
            pass
        raise


if not os.path.exists(CONFIG_FILE):
    os.makedirs(os.path.dirname(CONFIG_FILE), exist_ok=True)
    save_config(_default_config())


def _parse_env_value(raw_value: str) -> str:
    value = raw_value.strip()
    if len(value) >= 2 and ((value[0] == value[-1] == '"') or (value[0] == value[-1] == "'")):
        value = value[1:-1]
    return value


def _load_public_ap_env() -> dict:
    global _public_ap_env_cache
    path = PUBLIC_AP_ENV_FILE
    try:
        stat = os.stat(path)
        mtime = stat.st_mtime_ns
    except FileNotFoundError:
        _public_ap_env_cache = {"path": path, "mtime": None, "data": {}}
        return {}

    if (
        _public_ap_env_cache["path"] == path
        and _public_ap_env_cache["mtime"] == mtime
    ):
        return _public_ap_env_cache["data"]

    data = {}
    try:
        with open(path, "r", encoding="utf-8") as handle:
            for line in handle:
                stripped = line.strip()
                if not stripped or stripped.startswith("#"):
                    continue
                if "=" not in stripped:
                    continue
                key, raw_value = stripped.split("=", 1)
                key = key.strip()
                if not key:
                    continue
                data[key] = _parse_env_value(raw_value)
    except OSError:
        data = {}

    _public_ap_env_cache = {"path": path, "mtime": mtime, "data": data}
    return data


def _get_shutdown_token() -> Optional[str]:
    token = os.environ.get(SHUTDOWN_TOKEN_ENV_VAR)
    if isinstance(token, str) and token.strip():
        return token.strip()

    env_data = _load_public_ap_env()
    value = env_data.get(SHUTDOWN_TOKEN_ENV_VAR)
    if isinstance(value, str) and value.strip():
        return value.strip()
    return None


def _is_local_request(remote_addr: Optional[str]) -> bool:
    if not remote_addr:
        return False
    return remote_addr in _LOCALHOST_ADDRESSES


def _validate_shutdown_request() -> Optional[str]:
    if _is_local_request(request.remote_addr):
        return None

    token = _get_shutdown_token()
    if not token:
        return "Kein SHUTDOWN_TOKEN konfiguriert"

    provided = request.headers.get(SHUTDOWN_TOKEN_HEADER, "")
    if not provided:
        return "Authentifizierungs-Token fehlt"

    if not hmac.compare_digest(provided.strip(), token):
        return "Authentifizierungs-Token ungültig"

    return None


def _execute_poweroff() -> None:
    command = os.environ.get(SHUTDOWN_COMMAND_ENV_VAR)
    if command:
        os.system(command)
    else:
        os.system("poweroff")


def sanitize_hostname(hostname: Optional[str], *, fallback_prefix: str = "box") -> str:
    if isinstance(hostname, bytes):
        try:
            hostname = hostname.decode("utf-8", "ignore")
        except Exception:
            hostname = hostname.decode("latin-1", "ignore")
    if hostname is None:
        value = ""
    elif isinstance(hostname, str):
        value = hostname
    else:
        value = str(hostname)

    value = _ALLOWED_HOSTNAME_PATTERN.sub("-", value)
    value = re.sub(r"-{2,}", "-", value)
    value = re.sub(r"\.{2,}", ".", value)
    value = value.strip("-_.")

    if not value:
        value = f"{fallback_prefix}-{secrets.token_hex(4)}"

    return value[:64]


def sanitize_ipv4(value: Optional[str], *, placeholder: str = SAFE_IP_PLACEHOLDER) -> str:
    if isinstance(value, bytes):
        try:
            value = value.decode("utf-8", "ignore")
        except Exception:
            value = value.decode("latin-1", "ignore")
    if not isinstance(value, str):
        return placeholder

    candidate = value.strip()
    if not candidate:
        return placeholder

    try:
        address = ipaddress.IPv4Address(candidate)
    except ipaddress.AddressValueError:
        return placeholder

    return str(address)


def sanitize_letter(value, *, default: str = "") -> str:
    if value is None:
        return default

    if isinstance(value, bytes):
        try:
            value = value.decode("utf-8", "ignore")
        except Exception:
            value = value.decode("latin-1", "ignore")
    elif not isinstance(value, str):
        value = str(value)

    candidate = value.strip()
    if not candidate:
        return default

    first_char = candidate[0]
    if "a" <= first_char <= "z":
        first_char = first_char.upper()

    if first_char in _ALLOWED_LETTERS:
        return first_char

    return default


def _normalize_letter_list(values, legacy_value=None):
    normalized = ["" for _ in range(TRIGGER_SLOTS)]
    if isinstance(values, list):
        for idx in range(min(TRIGGER_SLOTS, len(values))):
            normalized[idx] = sanitize_letter(values[idx])
    elif isinstance(values, dict):
        for key, val in values.items():
            if str(key).isdigit():
                idx = int(str(key))
                if 0 <= idx < TRIGGER_SLOTS:
                    normalized[idx] = sanitize_letter(val)
    elif isinstance(values, str):
        normalized[0] = sanitize_letter(values)
    elif values is not None:
        normalized[0] = sanitize_letter(values)

    if isinstance(legacy_value, str) and not normalized[0]:
        legacy_sanitized = sanitize_letter(legacy_value)
        if legacy_sanitized:
            normalized[0] = legacy_sanitized
    return normalized


def _sanitize_color(value) -> str:
    if isinstance(value, str):
        candidate = value.strip()
        if _HEX_COLOR_PATTERN.match(candidate):
            return candidate.lower()
    return DEFAULT_COLOR


def _normalize_color_list(values, legacy_value=None):
    normalized = [DEFAULT_COLOR for _ in range(TRIGGER_SLOTS)]
    if isinstance(values, list):
        for idx in range(min(TRIGGER_SLOTS, len(values))):
            val = values[idx]
            normalized[idx] = _sanitize_color(val)
    elif isinstance(values, dict):
        for key, val in values.items():
            if str(key).isdigit():
                idx = int(str(key))
                if 0 <= idx < TRIGGER_SLOTS:
                    normalized[idx] = _sanitize_color(val)
    elif isinstance(values, str) and values:
        normalized[0] = _sanitize_color(values)

    if isinstance(legacy_value, str) and (not normalized[0] or normalized[0] == DEFAULT_COLOR):
        normalized[0] = _sanitize_color(legacy_value)
    return normalized


def _coerce_delay_value(value):
    if isinstance(value, (int, float)):
        numeric = float(value)
    elif isinstance(value, str):
        try:
            numeric = float(value.strip())
        except (ValueError, AttributeError):
            return DEFAULT_DELAY
    else:
        return DEFAULT_DELAY

    if numeric < 0:
        return DEFAULT_DELAY

    coerced = int(math.floor(numeric + 0.5))
    if coerced < 0:
        return DEFAULT_DELAY
    return min(999, coerced)


def _normalize_delay_list(values, legacy_value=None):
    normalized = [DEFAULT_DELAY for _ in range(TRIGGER_SLOTS)]
    if isinstance(values, list):
        for idx in range(min(TRIGGER_SLOTS, len(values))):
            normalized[idx] = _coerce_delay_value(values[idx])
    elif isinstance(values, dict):
        for key, val in values.items():
            if str(key).isdigit():
                idx = int(str(key))
                if 0 <= idx < TRIGGER_SLOTS:
                    normalized[idx] = _coerce_delay_value(val)
    elif isinstance(values, str):
        normalized[0] = _coerce_delay_value(values)
    elif values is not None:
        normalized[0] = _coerce_delay_value(values)

    if legacy_value is not None and normalized[0] == DEFAULT_DELAY:
        normalized[0] = _coerce_delay_value(legacy_value)
    return normalized


def _normalize_config_ips(data) -> bool:
    changed = False
    if isinstance(data, dict):
        boxen = data.get("boxen")
        if isinstance(boxen, dict):
            for box in boxen.values():
                if isinstance(box, dict):
                    sanitized_ip = sanitize_ipv4(box.get("ip"))
                    if box.get("ip") != sanitized_ip:
                        box["ip"] = sanitized_ip
                        changed = True
    return changed


def _default_delay_matrix():
    return {day: [DEFAULT_DELAY for _ in range(TRIGGER_SLOTS)] for day in DAYS}


def ensure_box_structure(box, remove_legacy=False):
    changed = False
    if not isinstance(box, dict):
        return True

    if "letters" not in box or not isinstance(box["letters"], dict):
        box["letters"] = {}
        changed = True
    if "colors" not in box or not isinstance(box["colors"], dict):
        box["colors"] = {}
        changed = True
    if "delays" not in box or not isinstance(box["delays"], dict):
        box["delays"] = {}
        changed = True

    for day_index, day in enumerate(DAYS):
        letters = box["letters"].get(day)
        legacy_letter = box.get(day)
        normalized_letters = _normalize_letter_list(letters, legacy_letter)
        if box["letters"].get(day) != normalized_letters:
            box["letters"][day] = normalized_letters
            changed = True

        colors = box["colors"].get(day)
        legacy_color = box.get(f"color_{day}")
        normalized_colors = _normalize_color_list(colors, legacy_color)
        if box["colors"].get(day) != normalized_colors:
            box["colors"][day] = normalized_colors
            changed = True

        delays = box["delays"].get(day)
        legacy_delay = box.get(f"delay_{day}")
        normalized_delays = _normalize_delay_list(delays, legacy_delay)
        if box["delays"].get(day) != normalized_delays:
            box["delays"][day] = normalized_delays
            changed = True

        if remove_legacy:
            if day in box:
                del box[day]
                changed = True
            legacy_color_key = f"color_{day}"
            if legacy_color_key in box:
                del box[legacy_color_key]
                changed = True
            legacy_delay_key = f"delay_{day}"
            if legacy_delay_key in box:
                del box[legacy_delay_key]
                changed = True

    return changed


def migrate_config(data):
    changed = False
    if not isinstance(data, dict):
        return False

    if "boxen" not in data or not isinstance(data["boxen"], dict):
        data["boxen"] = {}
        changed = True
    if "boxOrder" not in data or not isinstance(data["boxOrder"], list):
        data["boxOrder"] = []
        changed = True

    sanitized_boxen = {}
    rename_map = {}
    original_order = list(data["boxen"].keys())
    renamed_or_reordered = False
    for original_hostname, box in list(data["boxen"].items()):
        target = sanitize_hostname(original_hostname)
        suffix = 1
        candidate = target
        while candidate in sanitized_boxen and sanitized_boxen[candidate] is not box:
            suffix += 1
            candidate = sanitize_hostname(f"{target}-{suffix}")
        rename_map[original_hostname] = candidate
        sanitized_boxen[candidate] = box
        if candidate != original_hostname:
            renamed_or_reordered = True
        if ensure_box_structure(box, remove_legacy=True):
            changed = True

    if not renamed_or_reordered and original_order != list(sanitized_boxen.keys()):
        renamed_or_reordered = True

    if renamed_or_reordered:
        data["boxen"] = sanitized_boxen
        changed = True

    if data["boxOrder"]:
        new_order = []
        seen = set()
        for entry in data["boxOrder"]:
            mapped = rename_map.get(entry)
            if mapped is None:
                mapped = sanitize_hostname(entry)
            if mapped in sanitized_boxen and mapped not in seen:
                new_order.append(mapped)
                seen.add(mapped)
        if new_order != data["boxOrder"]:
            data["boxOrder"] = new_order
            changed = True

    return changed


def extract_box_state_from_soup(soup):
    letters = {day: ["" for _ in range(TRIGGER_SLOTS)] for day in DAYS}
    colors = {day: [DEFAULT_COLOR for _ in range(TRIGGER_SLOTS)] for day in DAYS}
    delays = {day: [DEFAULT_DELAY for _ in range(TRIGGER_SLOTS)] for day in DAYS}

    schema_cache = {}

    def _prefer_fallback(tag, base_name):
        key = (tag, base_name)
        if key not in schema_cache:
            result = False
            for field in soup.find_all(tag):
                name = field.get("name")
                if not name or not name.startswith(base_name):
                    continue
                suffix = name[len(base_name) :]
                if not suffix:
                    continue
                if suffix[0].isdigit() and "_" not in suffix:
                    result = True
                    break
                if suffix.startswith("_") and suffix[1:].isdigit():
                    result = True
                    break
            schema_cache[key] = result
        return schema_cache[key]

    used_fields = set()

    def _find_field(tag, base_name, firmware_day_index, slot, fallback_day_index=None):
        def build_candidates(index_value):
            names = []
            if slot == 0:
                names.extend(
                    [
                        f"{base_name}_{index_value}",
                        f"{base_name}{index_value}",
                    ]
                )
            if slot is not None:
                names.extend(
                    [
                        f"{base_name}_{slot}_{index_value}",
                        f"{base_name}{slot}_{index_value}",
                        f"{base_name}_{slot}{index_value}",
                        f"{base_name}{slot}{index_value}",
                        f"{base_name}_{index_value}_{slot}",
                        f"{base_name}{index_value}_{slot}",
                        f"{base_name}_{index_value}{slot}",
                        f"{base_name}{index_value}{slot}",
                    ]
                )
            return names

        candidate_groups = []

        if _prefer_fallback(tag, base_name):
            if fallback_day_index is not None:
                candidate_groups.append(build_candidates(fallback_day_index))
            if firmware_day_index is not None:
                candidate_groups.append(build_candidates(firmware_day_index))
        else:
            if firmware_day_index is not None:
                candidate_groups.append(build_candidates(firmware_day_index))
            if fallback_day_index is not None:
                candidate_groups.append(build_candidates(fallback_day_index))

        for group in candidate_groups:
            for candidate in dict.fromkeys(group):
                for field in soup.find_all(tag, {"name": candidate}):
                    identifier = id(field)
                    if identifier in used_fields:
                        continue
                    used_fields.add(identifier)
                    return field
        return None

    for day_index, day in enumerate(DAYS):
        firmware_day_index = DAY_TO_FIRMWARE_INDEX.get(day, day_index)
        for slot in range(TRIGGER_SLOTS):
            select = _find_field("select", "letter", firmware_day_index, slot, fallback_day_index=day_index)

            value = ""
            if select:
                selected = select.find("option", selected=True)
                if selected and selected.get("value") is not None:
                    value = selected.get("value", "")
                else:
                    first_option = select.find("option")
                    if first_option and first_option.get("value") is not None:
                        value = first_option.get("value", "")
            letters[day][slot] = sanitize_letter(value, default="")

            color_input = _find_field("input", "color", firmware_day_index, slot, fallback_day_index=day_index)

            color_value = DEFAULT_COLOR
            if color_input and color_input.has_attr("value") and color_input["value"]:
                color_value = color_input["value"]
            color_value = _sanitize_color(color_value)
            colors[day][slot] = color_value

            delay_input = _find_field("input", "delay", firmware_day_index, slot, fallback_day_index=day_index)

            delay_value = DEFAULT_DELAY
            if delay_input is not None:
                raw_value = delay_input.get("value")
                if raw_value is not None:
                    delay_value = _coerce_delay_value(raw_value)
            delays[day][slot] = delay_value

    return letters, colors, delays

def fetch_trigger_delays(ip):
    ip = sanitize_ipv4(ip)
    if ip == SAFE_IP_PLACEHOLDER:
        return None
    try:
        response = requests.get(
            f"http://{ip}/api/trigger-delays",
            timeout=3,
            allow_redirects=False,
        )
    except requests.RequestException:
        return None

    _ensure_no_redirect(response, action="Trigger-Delay-Abfrage", host=ip)

    if not response.ok:
        return None

    try:
        data = response.json()
    except ValueError:
        return None

    delays_payload = data.get("delays")
    if not isinstance(delays_payload, dict):
        return None

    normalized = {}
    for day in DAYS:
        day_payload = None
        if isinstance(delays_payload, dict):
            day_payload = delays_payload.get(day)
            if day_payload is None:
                firmware_index = DAY_TO_FIRMWARE_INDEX.get(day)
                if firmware_index is not None:
                    day_payload = delays_payload.get(str(firmware_index))
        normalized[day] = _normalize_delay_list(day_payload)
    return normalized


def get_connected_devices():
    devices = []
    config = load_config()
    if os.path.exists(LEASE_FILE):
        with open(LEASE_FILE, "r") as f:
            for line in f:
                parts = line.split()
                if len(parts) >= 3:
                    ip = sanitize_ipv4(parts[2])
                    if ip == SAFE_IP_PLACEHOLDER:
                        continue
                    try:
                        ping_rc = subprocess.call(
                            ["ping", "-c", "1", "-W", "1", ip],
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL,
                        )
                    except (FileNotFoundError, OSError) as exc:
                        app.logger.warning(
                            "Ping-Kommando nicht verfügbar (%s) – Lease %s wird übersprungen",
                            exc,
                            ip,
                        )
                        continue

                    if ping_rc == 0:
                        try:
                            r = requests.get(
                                f"http://{ip}/",
                                timeout=3,
                                allow_redirects=False,
                            )
                        except requests.RequestException:
                            continue

                        _ensure_no_redirect(r, action="Geräte-Scan", host=ip)

                        if not r.ok:
                            continue

                        soup = BeautifulSoup(r.text, "html.parser")
                        hostname_field = soup.find("input", {"name": "hostname"})
                        if hostname_field is None:
                            continue

                        hostname = get_hostname_from_web(ip)
                        if hostname == "Unbekannt":
                            continue

                        hostname = sanitize_hostname(hostname)

                        hostname_exists = False
                        for existing_identifier, box in list(config["boxen"].items()):
                            if existing_identifier == hostname:
                                hostname_exists = True

                                if box["ip"] != ip:
                                    config["boxen"][hostname]["ip"] = ip
                                    save_config(config)
                                break

                        ip_exists = False
                        for existing_identifier, box in list(config["boxen"].items()):
                            if box["ip"] == ip and existing_identifier != hostname:
                                ip_exists = True

                                config["boxen"][existing_identifier]["ip"] = SAFE_IP_PLACEHOLDER
                                save_config(config)
                                break

                        devices.append({"ip": ip, "hostname": hostname})

                        if not hostname_exists:
                            learn_box(ip, hostname)
    return devices

def get_hostname_from_web(ip):
    ip = sanitize_ipv4(ip)
    if ip == SAFE_IP_PLACEHOLDER:
        return "Unbekannt"
    try:
        r = requests.get(
            f"http://{ip}/",
            timeout=3,
            allow_redirects=False,
        )
    except requests.RequestException:
        return "Unbekannt"

    _ensure_no_redirect(r, action="Hostname-Abfrage", host=ip)

    if r.ok:
        soup = BeautifulSoup(r.text, "html.parser")
        hostname_field = soup.find("input", {"name": "hostname"})
        if hostname_field is not None:
            value = hostname_field.get("value")
            if value is not None:
                if isinstance(value, str):
                    value = value.strip()
                if value:
                    return value
    return "Unbekannt"

def learn_box(ip, identifier):
    ip = sanitize_ipv4(ip)
    if ip == SAFE_IP_PLACEHOLDER:
        return
    identifier = sanitize_hostname(identifier)
    config = load_config()
    if identifier in config["boxen"]:
        box = config["boxen"][identifier]
        changed = False
        if box.get("ip") != ip:
            box["ip"] = ip
            changed = True
        if ensure_box_structure(box, remove_legacy=True):
            changed = True
        if changed:
            save_config(config)
        return

    try:
        r = requests.get(
            f"http://{ip}/",
            timeout=3,
            allow_redirects=False,
        )
    except requests.RequestException:
        return

    _ensure_no_redirect(r, action="Box-Lernvorgang", host=ip)

    if not r.ok:
        return

    soup = BeautifulSoup(r.text, "html.parser")
    letters, colors, html_delays = extract_box_state_from_soup(soup)

    try:
        delays = fetch_trigger_delays(ip)
    except RedirectResponseError:
        raise
    except Exception:
        delays = None

    if delays is None:
        delays = html_delays

    box = {
        "ip": ip,
        "letters": letters,
        "colors": colors,
        "delays": delays if delays is not None else _default_delay_matrix(),
    }
    ensure_box_structure(box, remove_legacy=True)
    config["boxen"][identifier] = box
    if identifier not in config["boxOrder"]:
        config["boxOrder"].append(identifier)
    save_config(config)

@app.route("/")
def index():
    with open("/usr/local/etc/index.html", "r") as f:
        return f.read()


@app.route("/vendor/<path:filename>")
def vendor_static(filename: str):
    return send_from_directory("/usr/local/etc/vendor", filename)


@app.route("/devices")
def devices():
    try:
        connected = get_connected_devices()
    except RedirectResponseError as exc:
        return _redirect_error_response(exc)
    config = load_config()
    return jsonify({"boxen": config["boxen"], "connected": connected, "boxOrder": config["boxOrder"]})

@app.route("/update_box", methods=["POST"])
def update_box():

    data = request.json or {}
    raw_hostname = data.get("hostname")
    if not raw_hostname:
        return jsonify({"status": "error"}), 400

    hostname = sanitize_hostname(raw_hostname)
    config = load_config()
    renamed_existing = False
    if hostname != raw_hostname and raw_hostname in config["boxen"]:
        config["boxen"][hostname] = config["boxen"].pop(raw_hostname)
        config["boxOrder"] = [hostname if h == raw_hostname else h for h in config["boxOrder"]]
        renamed_existing = True

    box = config["boxen"].setdefault(hostname, {"ip": SAFE_IP_PLACEHOLDER})
    changed = ensure_box_structure(box, remove_legacy=True) or renamed_existing

    updated = False

    if "ip" in data and isinstance(data.get("ip"), str):
        sanitized_ip = sanitize_ipv4(data.get("ip"))
        if box.get("ip") != sanitized_ip:
            box["ip"] = sanitized_ip
            updated = True

    letters_payload = data.get("letters")
    if isinstance(letters_payload, dict):
        for day, values in letters_payload.items():
            if day in DAYS:
                normalized = _normalize_letter_list(values)
                if box["letters"].get(day) != normalized:
                    box["letters"][day] = normalized
                    updated = True

    for day in DAYS:
        legacy_letter_value = data.get(day)
        if isinstance(legacy_letter_value, str):
            normalized = _normalize_letter_list([legacy_letter_value])
            if box["letters"].get(day) != normalized:
                box["letters"][day] = normalized
                updated = True

    colors_payload = data.get("colors")
    if isinstance(colors_payload, dict):
        for day, values in colors_payload.items():
            if day in DAYS:
                normalized = _normalize_color_list(values)
                if box["colors"].get(day) != normalized:
                    box["colors"][day] = normalized
                    updated = True

    for day in DAYS:
        legacy_color_value = data.get(f"color_{day}")
        if isinstance(legacy_color_value, str):
            normalized = _normalize_color_list([legacy_color_value])
            if box["colors"].get(day) != normalized:
                box["colors"][day] = normalized
                updated = True

    delays_payload = data.get("delays")
    if isinstance(delays_payload, dict):
        for day, values in delays_payload.items():
            if day in DAYS:
                normalized = _normalize_delay_list(values)
                if box["delays"].get(day) != normalized:
                    box["delays"][day] = normalized
                    updated = True

    for day in DAYS:
        if f"delay_{day}" in data:
            normalized = _normalize_delay_list([data.get(f"delay_{day}")])
            if box["delays"].get(day) != normalized:
                box["delays"][day] = normalized
                updated = True

    day = data.get("day")
    trigger_index = data.get("triggerIndex")
    try:
        trigger_index = int(trigger_index)
    except (TypeError, ValueError):
        trigger_index = None

    if day in DAYS and isinstance(trigger_index, int) and 0 <= trigger_index < TRIGGER_SLOTS:
        if "letter" in data and isinstance(data.get("letter"), str):
            sanitized_letter = sanitize_letter(data["letter"])
            if box["letters"][day][trigger_index] != sanitized_letter:
                box["letters"][day][trigger_index] = sanitized_letter
                updated = True
        if "color" in data and isinstance(data.get("color"), str):
            color_value = _sanitize_color(data["color"])
            if box["colors"][day][trigger_index] != color_value:
                box["colors"][day][trigger_index] = color_value
                updated = True
        if "delay" in data:
            delay_value = _coerce_delay_value(data.get("delay"))
            if box["delays"][day][trigger_index] != delay_value:
                box["delays"][day][trigger_index] = delay_value
                updated = True

    if changed or updated:
        save_config(config)
    return jsonify({"status": "success"})

@app.route("/remove_box", methods=["POST"])
def remove_box():

    data = request.get_json(silent=True) or {}
    raw_hostname = data.get("hostname")
    if not raw_hostname:
        return jsonify({"status": "removed"})

    hostname = sanitize_hostname(raw_hostname)
    config = load_config()
    removed = False
    for candidate in {hostname, raw_hostname}:
        if candidate in config["boxen"]:
            del config["boxen"][candidate]
            removed = True
    if removed:
        config["boxOrder"] = [h for h in config["boxOrder"] if h not in {hostname, raw_hostname}]
        save_config(config)
    return jsonify({"status": "removed"})

@app.route("/update_box_order", methods=["POST"])
def update_box_order():

    data = request.get_json(silent=True) or {}
    boxOrder = data.get("boxOrder")
    if isinstance(boxOrder, list):
        config = load_config()
        sanitized_order = []
        seen = set()
        for entry in boxOrder:
            sanitized = sanitize_hostname(entry)
            candidate = None
            if sanitized in config["boxen"]:
                candidate = sanitized
            elif entry in config["boxen"]:
                candidate = entry
            if candidate and candidate not in seen:
                sanitized_order.append(candidate)
                seen.add(candidate)
        config["boxOrder"] = sanitized_order
        save_config(config)
        return jsonify({"status": "success"})
    return jsonify({"status": "error"}), 400

@app.route("/reload_all", methods=["POST"])
def reload_all():

    config = load_config()
    config["boxen"] = {}
    config["boxOrder"] = []
    save_config(config)
    try:
        get_connected_devices()
    except RedirectResponseError as exc:
        return _redirect_error_response(exc)
    return jsonify({"status": "reloaded"})

@app.route("/transfer_box", methods=["POST"])
def transfer_box():

    payload = request.get_json(silent=True) or {}
    raw_hostname = payload.get("hostname") or request.args.get("hostname")
    if not raw_hostname:
        return jsonify({"status": "❌ Hostname fehlt"}), 400
    hostname = sanitize_hostname(raw_hostname)
    config = load_config()
    box = config["boxen"].get(hostname)
    renamed_existing = False
    if box is None and raw_hostname != hostname and raw_hostname in config["boxen"]:
        box = config["boxen"].pop(raw_hostname)
        config["boxen"][hostname] = box
        config["boxOrder"] = [hostname if h == raw_hostname else h for h in config["boxOrder"]]
        renamed_existing = True
    if not box:
        return jsonify({"status": "❌ Box unbekannt"})

    changed = ensure_box_structure(box, remove_legacy=True)
    if changed or renamed_existing:
        save_config(config)

    ip = box.get("ip")
    if sanitize_ipv4(ip) == SAFE_IP_PLACEHOLDER:
        return jsonify({"status": "❌ IP unbekannt"})
    if not ip:
        return jsonify({"status": "❌ IP unbekannt"})

    try:
        r = requests.get(f"http://{ip}/", timeout=3, allow_redirects=False)
    except requests.RequestException:
        return jsonify({"status": "❌ Box nicht erreichbar"})
    if (
        getattr(r, "is_redirect", False)
        or getattr(r, "is_permanent_redirect", False)
        or 300 <= getattr(r, "status_code", 0) < 400
    ):
        app.logger.warning(
            "Transfer-Box für %s abgebrochen: unerwartete Weiterleitung (HTTP %s)",
            hostname,
            r.status_code,
        )
        return (
            jsonify(
                {
                    "status": "❌ Unerwartete Weiterleitung",
                    "details": f"Box antwortete mit HTTP {r.status_code} und Weiterleitung",
                }
            ),
            502,
        )
    if not r.ok:
        return jsonify({"status": "❌ Box nicht erreichbar"})

    soup = BeautifulSoup(r.text, "html.parser")
    remote_letters, remote_colors, _ = extract_box_state_from_soup(soup)
    try:
        remote_delays = fetch_trigger_delays(ip)
    except RedirectResponseError as exc:
        return _redirect_error_response(exc)

    stored_letters = {day: list(box["letters"][day]) for day in DAYS}
    stored_colors = {day: list(box["colors"][day]) for day in DAYS}
    stored_delays = {day: [_coerce_delay_value(value) for value in box["delays"][day]] for day in DAYS}

    if (
        remote_letters == stored_letters
        and remote_colors == stored_colors
        and (remote_delays is not None and remote_delays == stored_delays)
    ):
        return jsonify({"status": "⏭️ Bereits aktuell"})

    payload = {
        "letters": stored_letters,
        "colors": stored_colors,
        "delays": stored_delays,
    }

    try:
        r = requests.post(
            f"http://{ip}/updateAllLetters",
            json=payload,
            timeout=3,
            allow_redirects=False,
        )
    except requests.RequestException:
        return jsonify({"status": "❌ Fehler bei Übertragung"})
    if (
        getattr(r, "is_redirect", False)
        or getattr(r, "is_permanent_redirect", False)
        or 300 <= getattr(r, "status_code", 0) < 400
    ):
        app.logger.warning(
            "Transfer-Box für %s abgebrochen: unerwartete Weiterleitung beim Update (HTTP %s)",
            hostname,
            r.status_code,
        )
        return (
            jsonify(
                {
                    "status": "❌ Unerwartete Weiterleitung",
                    "details": f"Update-Anfrage erhielt HTTP {r.status_code} mit Weiterleitung",
                }
            ),
            502,
        )
    if not r.ok:
        return jsonify({"status": "❌ Fehler bei Übertragung"})

    return jsonify({"status": "✅ Übertragen"})

@app.route("/shutdown", methods=["POST"])
def shutdown():
    denial_reason = _validate_shutdown_request()
    if denial_reason:
        remote_addr = request.remote_addr or "unbekannt"
        app.logger.warning(
            "Shutdown-Anfrage abgelehnt: %s (Remote: %s)",
            denial_reason,
            remote_addr,
        )
        return (
            jsonify({"status": "❌ Zugriff verweigert", "details": denial_reason}),
            403,
        )

    _execute_poweroff()
    return jsonify({"status": "OK"}), 200


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=8080)
