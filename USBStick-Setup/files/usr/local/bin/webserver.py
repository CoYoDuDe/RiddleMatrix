#!/usr/bin/env python3
from flask import Flask, abort, jsonify, request, send_from_directory
from bs4 import BeautifulSoup
import argparse
import math
import os, json, subprocess, requests
import tempfile
import secrets
import re
import ipaddress
import copy
import shutil
import socket
import shlex
from functools import lru_cache
from typing import Optional

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
DEFAULT_COLOR_MODE = "fixed"
DEFAULT_COLOR_PALETTE_MASK = 0xFF

_HEX_COLOR_PATTERN = re.compile(r"^#[0-9A-Fa-f]{6}$")

_ALLOWED_HOSTNAME_PATTERN = re.compile(r"[^A-Za-z0-9._-]+")

SAFE_IP_PLACEHOLDER = "0.0.0.0"

_FIRMWARE_LETTERS = tuple("ABCDEFGHIJKLMNOPQRSTUVWXYZ*#~&?01234567")
_ALLOWED_LETTERS = set(_FIRMWARE_LETTERS)
_ALLOWED_COLOR_MODES = {"fixed", "random_selected", "random_all"}
_SYMBOL_BITMAP_HEX_PATTERN = re.compile(r"^[0-9A-Fa-f]{256}$")

MAX_HOSTNAME_LENGTH = 64

app = Flask(__name__)
LEASE_FILE = os.environ.get("RIDDLEMATRIX_LEASE_FILE", "/var/lib/misc/dnsmasq.leases")
CONFIG_FILE = os.environ.get("RIDDLEMATRIX_CONFIG_FILE", "/mnt/persist/boxen_config/boxen_config.json")
INDEX_FILE = os.environ.get("RIDDLEMATRIX_INDEX_FILE", "/usr/local/etc/index.html")
VENDOR_DIR = os.environ.get("RIDDLEMATRIX_VENDOR_DIR", "/usr/local/etc/vendor")
SCAN_SUBNET = os.environ.get("RIDDLEMATRIX_SCAN_SUBNET", "192.168.137")
ENABLE_ARP_SCAN = os.environ.get("RIDDLEMATRIX_ENABLE_ARP_SCAN", "").strip().lower() in {"1", "true", "yes", "on"}
SERVER_HOST = os.environ.get("RIDDLEMATRIX_SERVER_HOST", "0.0.0.0")
SERVER_PORT = int(os.environ.get("RIDDLEMATRIX_SERVER_PORT", "8080"))
SHUTDOWN_COMMAND_ENV_VAR = "SHUTDOWN_COMMAND"
HIDE_SHUTDOWN = os.environ.get("RIDDLEMATRIX_HIDE_SHUTDOWN", "").strip().lower() in {"1", "true", "yes", "on"}
BOX_MANAGER_KEY_HEADER = "X-RiddleMatrix-Manager-Key"


def _read_public_ap_passphrase() -> str:
    for path in (
        os.environ.get("RIDDLEMATRIX_PUBLIC_AP_ENV", ""),
        "/etc/usbstick/public_ap.env",
        os.path.join(os.path.dirname(CONFIG_FILE), "public_ap.env"),
    ):
        if not path:
            continue
        try:
            with open(path, "r", encoding="utf-8") as env_file:
                for raw_line in env_file:
                    line = raw_line.strip()
                    if not line or line.startswith("#") or "=" not in line:
                        continue
                    key, value = line.split("=", 1)
                    if key.strip() == "WPA_PASSPHRASE":
                        parsed = shlex.split(value, comments=False, posix=True)
                        return parsed[0] if parsed else value.strip().strip("\"'")
        except OSError:
            continue
        except ValueError:
            continue
    return ""


def get_box_manager_key() -> str:
    key = os.environ.get("RIDDLEMATRIX_BOX_MANAGER_KEY", "").strip()
    if key:
        return key
    key = _read_public_ap_passphrase().strip()
    if key:
        return key
    return "RiddleMatrix-Setup!"


def box_manager_headers(extra=None) -> dict:
    headers = dict(extra or {})
    key = get_box_manager_key()
    if key:
        headers[BOX_MANAGER_KEY_HEADER] = key
    return headers


def box_manager_url(url: str) -> str:
    key = get_box_manager_key()
    if not key:
        return url
    separator = "&" if "?" in url else "?"
    from urllib.parse import quote
    return f"{url}{separator}rm_key={quote(key, safe='')}"


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

    return value[:MAX_HOSTNAME_LENGTH]


def _allocate_unique_hostname(base_name: str, config: dict) -> str:
    target = sanitize_hostname(base_name)
    if not isinstance(config, dict):
        return target

    boxen = config.get("boxen")
    if not isinstance(boxen, dict):
        return target

    if target not in boxen:
        return target

    suffix = 2
    while True:
        suffix_str = str(suffix)
        max_base_len = MAX_HOSTNAME_LENGTH - len(suffix_str) - 1
        if max_base_len > 0:
            base_part = target[:max_base_len]
            candidate = f"{base_part}-{suffix_str}"
        else:
            candidate = suffix_str[-MAX_HOSTNAME_LENGTH:]

        if candidate not in boxen:
            return candidate

        suffix += 1


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


def _sanitize_color_mode(value) -> str:
    if isinstance(value, str):
        candidate = value.strip().lower()
        if candidate in _ALLOWED_COLOR_MODES:
            return candidate
    return DEFAULT_COLOR_MODE


def _normalize_color_mode_list(values):
    normalized = [DEFAULT_COLOR_MODE for _ in range(TRIGGER_SLOTS)]
    if isinstance(values, list):
        for idx in range(min(TRIGGER_SLOTS, len(values))):
            normalized[idx] = _sanitize_color_mode(values[idx])
    elif isinstance(values, dict):
        for key, val in values.items():
            if str(key).isdigit():
                idx = int(str(key))
                if 0 <= idx < TRIGGER_SLOTS:
                    normalized[idx] = _sanitize_color_mode(val)
    elif isinstance(values, str):
        normalized[0] = _sanitize_color_mode(values)
    return normalized


def _sanitize_color_palette_mask(value) -> int:
    try:
        if isinstance(value, str):
            candidate = int(value.strip(), 10)
        else:
            candidate = int(value)
    except (TypeError, ValueError):
        return DEFAULT_COLOR_PALETTE_MASK

    if candidate < 0:
        return DEFAULT_COLOR_PALETTE_MASK

    return candidate & DEFAULT_COLOR_PALETTE_MASK


def _normalize_color_palette_mask_list(values):
    normalized = [DEFAULT_COLOR_PALETTE_MASK for _ in range(TRIGGER_SLOTS)]
    if isinstance(values, list):
        for idx in range(min(TRIGGER_SLOTS, len(values))):
            normalized[idx] = _sanitize_color_palette_mask(values[idx])
    elif isinstance(values, dict):
        for key, val in values.items():
            if str(key).isdigit():
                idx = int(str(key))
                if 0 <= idx < TRIGGER_SLOTS:
                    normalized[idx] = _sanitize_color_palette_mask(val)
    elif values is not None:
        normalized[0] = _sanitize_color_palette_mask(values)
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


def _default_color_mode_matrix():
    return {day: [DEFAULT_COLOR_MODE for _ in range(TRIGGER_SLOTS)] for day in DAYS}


def _default_color_palette_mask_matrix():
    return {day: [DEFAULT_COLOR_PALETTE_MASK for _ in range(TRIGGER_SLOTS)] for day in DAYS}


def _box_color_modes(box):
    matrix = _default_color_mode_matrix()
    if isinstance(box, dict) and isinstance(box.get("colorModes"), dict):
        for day in DAYS:
            matrix[day] = _normalize_color_mode_list(box["colorModes"].get(day))
    return matrix


def _box_color_palette_masks(box):
    matrix = _default_color_palette_mask_matrix()
    if isinstance(box, dict) and isinstance(box.get("colorPaletteMasks"), dict):
        for day in DAYS:
            matrix[day] = _normalize_color_palette_mask_list(box["colorPaletteMasks"].get(day))
    return matrix


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
    if "colorModes" in box and not isinstance(box["colorModes"], dict):
        box["colorModes"] = {}
        changed = True
    if "colorPaletteMasks" in box and not isinstance(box["colorPaletteMasks"], dict):
        box["colorPaletteMasks"] = {}
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

        if isinstance(box.get("colorModes"), dict):
            normalized_color_modes = _normalize_color_mode_list(box["colorModes"].get(day))
            if box["colorModes"].get(day) != normalized_color_modes:
                box["colorModes"][day] = normalized_color_modes
                changed = True

        if isinstance(box.get("colorPaletteMasks"), dict):
            normalized_palette_masks = _normalize_color_palette_mask_list(box["colorPaletteMasks"].get(day))
            if box["colorPaletteMasks"].get(day) != normalized_palette_masks:
                box["colorPaletteMasks"][day] = normalized_palette_masks
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


def extract_box_color_settings_from_soup(soup):
    color_modes = _default_color_mode_matrix()
    color_palette_masks = _default_color_palette_mask_matrix()

    for day_index, day in enumerate(DAYS):
        firmware_day_index = DAY_TO_FIRMWARE_INDEX.get(day, day_index)
        for slot in range(TRIGGER_SLOTS):
            mode_names = [
                f"color_mode_{slot}_{firmware_day_index}",
                f"color_mode_{slot}_{day_index}",
            ]
            mode_field = None
            for candidate in mode_names:
                mode_field = soup.find("select", {"name": candidate})
                if mode_field is not None:
                    break

            mode_value = DEFAULT_COLOR_MODE
            if mode_field is not None:
                selected = mode_field.find("option", selected=True)
                if selected and selected.get("value") is not None:
                    mode_value = selected.get("value", "")
                else:
                    first_option = mode_field.find("option")
                    if first_option and first_option.get("value") is not None:
                        mode_value = first_option.get("value", "")
            color_modes[day][slot] = _sanitize_color_mode(mode_value)

            palette_mask = 0
            for palette_index in range(8):
                checkbox_names = [
                    f"palette_{slot}_{firmware_day_index}_{palette_index}",
                    f"palette_{slot}_{day_index}_{palette_index}",
                ]
                for candidate in checkbox_names:
                    checkbox = soup.find("input", {"name": candidate})
                    if checkbox is None:
                        continue
                    if checkbox.has_attr("checked"):
                        palette_mask |= 1 << palette_index
                    break

            color_palette_masks[day][slot] = palette_mask if palette_mask else DEFAULT_COLOR_PALETTE_MASK

    return color_modes, color_palette_masks

def fetch_trigger_delays(ip):
    ip = sanitize_ipv4(ip)
    if ip == SAFE_IP_PLACEHOLDER:
        return None
    try:
        response = requests.get(
            f"http://{ip}/api/trigger-delays",
            headers=box_manager_headers(),
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


def _ping_host(ip: str) -> bool:
    command = ["ping", "-n", "1", "-w", "1000", ip] if os.name == "nt" else ["ping", "-c", "1", "-W", "1", ip]
    try:
        return subprocess.call(
            command,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        ) == 0
    except (FileNotFoundError, OSError) as exc:
        app.logger.warning(
            "Ping-Kommando nicht verfuegbar (%s) - %s wird per HTTP probiert",
            exc,
            ip,
        )
        return True


def _quick_http_probe(ip: str) -> bool:
    try:
        with socket.create_connection((ip, 80), timeout=0.08):
            pass
    except OSError:
        return False

    try:
        response = requests.get(
            f"http://{ip}/api/hello",
            timeout=0.45,
            allow_redirects=False,
        )
    except requests.RequestException:
        return False

    try:
        _ensure_no_redirect(response, action="Schnelltest", host=ip)
    except RedirectResponseError:
        return False

    if not response.ok:
        return False
    try:
        data = response.json()
    except ValueError:
        return False
    return isinstance(data, dict) and bool(data.get("riddleMatrix"))


def get_hello_hostname(ip: str) -> str:
    ip = sanitize_ipv4(ip)
    if ip == SAFE_IP_PLACEHOLDER:
        return ""
    try:
        response = requests.get(
            f"http://{ip}/api/hello",
            timeout=1.5,
            allow_redirects=False,
        )
    except requests.RequestException:
        return ""

    try:
        _ensure_no_redirect(response, action="Hello-Abfrage", host=ip)
    except RedirectResponseError:
        return ""

    if not response.ok:
        return ""
    try:
        data = response.json()
    except ValueError:
        return ""
    if not isinstance(data, dict) or not data.get("riddleMatrix"):
        return ""
    return sanitize_hostname(data.get("hostname") or "")


def _iter_arp_scan_ips():
    ips = set()
    try:
        arp_output = subprocess.check_output(["arp", "-a"], text=True, stderr=subprocess.DEVNULL)
    except (FileNotFoundError, OSError, subprocess.SubprocessError):
        arp_output = ""

    pattern = re.compile(rf"({re.escape(SCAN_SUBNET)}\.\d{{1,3}})")
    for match in pattern.finditer(arp_output):
        ip = sanitize_ipv4(match.group(1))
        if ip != SAFE_IP_PLACEHOLDER:
            ips.add(ip)

    for host_part in range(2, 255):
        ips.add(f"{SCAN_SUBNET}.{host_part}")
    return sorted(ips, key=lambda value: tuple(int(part) for part in value.split(".")))


def _inspect_candidate_device(ip: str, config: dict):
    if not _quick_http_probe(ip):
        return None, config

    try:
        r = requests.get(
            f"http://{ip}/",
            headers=box_manager_headers(),
            timeout=1.5,
            allow_redirects=False,
        )
    except requests.RequestException:
        return None, config

    _ensure_no_redirect(r, action="Geräte-Scan", host=ip)

    hostname = ""
    if r.ok:
        soup = BeautifulSoup(r.text, "html.parser")
        hostname_field = soup.find("input", {"name": "hostname"})
        if hostname_field is not None:
            hostname = get_hostname_from_web(ip)

    if not hostname or hostname == "Unbekannt":
        hostname = get_hello_hostname(ip)

    if not hostname:
        return None, config

    hostname = sanitize_hostname(hostname)
    boxen = config.get("boxen", {})
    identifier = _allocate_unique_hostname(hostname, config)

    existing_box = boxen.get(identifier)
    if isinstance(existing_box, dict) and existing_box.get("ip") != ip:
        existing_box["ip"] = ip
        save_config(config)

    for existing_identifier, box in list(boxen.items()):
        if (
            isinstance(box, dict)
            and box.get("ip") == ip
            and existing_identifier != identifier
        ):
            box["ip"] = SAFE_IP_PLACEHOLDER
            save_config(config)
            break

    if existing_box is None:
        learn_box(ip, identifier)
        config = load_config()

    return {"ip": ip, "hostname": identifier}, config


def get_connected_devices_by_scan():
    devices = []
    config = load_config()
    for ip in _iter_arp_scan_ips():
        device, config = _inspect_candidate_device(ip, config)
        if device:
            devices.append(device)
    return devices


def get_connected_devices():
    devices = []
    config = load_config()
    if not os.path.exists(LEASE_FILE):
        return get_connected_devices_by_scan() if ENABLE_ARP_SCAN else devices
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

                    if ping_rc != 0 and not _quick_http_probe(ip):
                        continue

                    try:
                        r = requests.get(
                            f"http://{ip}/",
                            headers=box_manager_headers(),
                            timeout=3,
                            allow_redirects=False,
                        )
                    except requests.RequestException:
                        continue

                    _ensure_no_redirect(r, action="Geräte-Scan", host=ip)

                    hostname = ""
                    if r.ok:
                        soup = BeautifulSoup(r.text, "html.parser")
                        hostname_field = soup.find("input", {"name": "hostname"})
                        if hostname_field is not None:
                            hostname = get_hostname_from_web(ip)

                    if not hostname or hostname == "Unbekannt":
                        hostname = get_hello_hostname(ip)

                    if not hostname:
                        continue

                    hostname = sanitize_hostname(hostname)
                    boxen = config.get("boxen", {})
                    identifier = _allocate_unique_hostname(hostname, config)

                    existing_box = boxen.get(identifier)
                    if isinstance(existing_box, dict) and existing_box.get("ip") != ip:
                        existing_box["ip"] = ip
                        save_config(config)

                    for existing_identifier, box in list(boxen.items()):
                        if (
                            isinstance(box, dict)
                            and box.get("ip") == ip
                            and existing_identifier != identifier
                        ):
                            box["ip"] = SAFE_IP_PLACEHOLDER
                            save_config(config)
                            break

                    devices.append({"ip": ip, "hostname": identifier})

                    if existing_box is None:
                        learn_box(ip, identifier)
                        config = load_config()
    return devices

def get_hostname_from_web(ip):
    ip = sanitize_ipv4(ip)
    if ip == SAFE_IP_PLACEHOLDER:
        return "Unbekannt"
    try:
        r = requests.get(
            f"http://{ip}/",
            headers=box_manager_headers(),
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
            headers=box_manager_headers(),
            timeout=3,
            allow_redirects=False,
        )
    except requests.RequestException:
        return

    _ensure_no_redirect(r, action="Box-Lernvorgang", host=ip)

    if r.ok:
        soup = BeautifulSoup(r.text, "html.parser")
        letters, colors, html_delays = extract_box_state_from_soup(soup)
        color_modes, color_palette_masks = extract_box_color_settings_from_soup(soup)
    else:
        letters = {day: ["" for _ in range(TRIGGER_SLOTS)] for day in DAYS}
        colors = {day: [DEFAULT_COLOR for _ in range(TRIGGER_SLOTS)] for day in DAYS}
        html_delays = _default_delay_matrix()
        color_modes = _default_color_mode_matrix()
        color_palette_masks = _default_color_palette_mask_matrix()

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
    if color_modes != _default_color_mode_matrix():
        box["colorModes"] = color_modes
    if color_palette_masks != _default_color_palette_mask_matrix():
        box["colorPaletteMasks"] = color_palette_masks
    ensure_box_structure(box, remove_legacy=True)
    config["boxen"][identifier] = box
    if identifier not in config["boxOrder"]:
        config["boxOrder"].append(identifier)
    save_config(config)


def _subnet_prefix_from_ip(value: str) -> Optional[str]:
    try:
        ip = ipaddress.ip_address(str(value).strip())
    except ValueError:
        return None
    if ip.version != 4 or ip.is_loopback or ip.is_link_local or ip.is_unspecified:
        return None
    parts = str(ip).split(".")
    return ".".join(parts[:3])


def _local_ipv4_subnet_candidates() -> list[str]:
    candidates: list[str] = []

    def add(value: str) -> None:
        prefix = _subnet_prefix_from_ip(value)
        if prefix is None:
            try:
                prefix = str(ipaddress.ip_network(f"{str(value).strip()}.0/24", strict=False).network_address)
                prefix = ".".join(prefix.split(".")[:3])
            except ValueError:
                prefix = None
        if prefix and prefix not in candidates:
            candidates.append(prefix)

    add(SCAN_SUBNET)
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.connect(("8.8.8.8", 80))
            add(probe.getsockname()[0])
    except OSError:
        pass

    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            add(info[4][0])
    except OSError:
        pass

    try:
        config = load_config()
        for box in config.get("boxen", {}).values():
            if isinstance(box, dict):
                add(str(box.get("ip", "")))
    except Exception:
        app.logger.exception("Lokale Netzbereiche konnten nicht aus der Konfiguration gelesen werden")

    return candidates

@app.route("/")
def index():
    with open(INDEX_FILE, "r", encoding="utf-8") as f:
        html = f.read()
    if HIDE_SHUTDOWN:
        html = html.replace('<button id="shutdown" onclick="shutdown()">Herunterfahren</button>', '')
    return html


@app.route("/vendor/<path:filename>")
def vendor_static(filename: str):
    return send_from_directory(VENDOR_DIR, filename)


@app.route("/webspace-config.js")
def webspace_config():
    manager_key_json = json.dumps(get_box_manager_key())
    return (
        "window.RIDDLEMATRIX_WEBSPACE_AUTH_SHA256 = "
        "window.RIDDLEMATRIX_WEBSPACE_AUTH_SHA256 || '';\n"
        "window.RIDDLEMATRIX_BOX_MANAGER_KEY = "
        f"window.RIDDLEMATRIX_BOX_MANAGER_KEY || {manager_key_json};\n",
        200,
        {"Content-Type": "application/javascript; charset=utf-8"},
    )


@app.route("/local_networks")
def local_networks():
    return jsonify({"subnets": _local_ipv4_subnet_candidates(), "defaultSubnet": SCAN_SUBNET})


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
    color_modes_matrix = _box_color_modes(box)
    color_palette_masks_matrix = _box_color_palette_masks(box)

    updated = False
    pending_letter_updates = {}

    def _current_letters_snapshot(day: str):
        if day in pending_letter_updates:
            return list(pending_letter_updates[day])
        existing = box["letters"].get(day)
        if isinstance(existing, list):
            return list(existing)
        return ["" for _ in range(TRIGGER_SLOTS)]

    def _collect_letter_update(day: str, values, source: str):
        current_letters = _current_letters_snapshot(day)
        new_letters = current_letters[:]
        changed_local = False

        def _assign(index: int, raw_value):
            nonlocal changed_local
            sanitized = sanitize_letter(raw_value)
            if sanitized == "":
                raw_repr = repr(raw_value)
                return (
                    f"Ungültiges Zeichen/Symbol für {day} (Trigger {index + 1}, Quelle: {source}) – Eingabe {raw_repr} wird abgelehnt."
                )
            if new_letters[index] != sanitized:
                new_letters[index] = sanitized
                changed_local = True
            return None

        if isinstance(values, list):
            for idx, raw in enumerate(values):
                if idx >= TRIGGER_SLOTS:
                    continue
                error_message = _assign(idx, raw)
                if error_message:
                    return None, error_message
        elif isinstance(values, dict):
            for key, raw in values.items():
                key_str = str(key)
                if key_str.isdigit():
                    idx = int(key_str)
                    if 0 <= idx < TRIGGER_SLOTS:
                        error_message = _assign(idx, raw)
                        if error_message:
                            return None, error_message
        elif isinstance(values, str) or values is not None:
            error_message = _assign(0, values)
            if error_message:
                return None, error_message
        else:
            return None, None

        if changed_local:
            return new_letters, None
        return None, None

    if "ip" in data and isinstance(data.get("ip"), str):
        sanitized_ip = sanitize_ipv4(data.get("ip"))
        if box.get("ip") != sanitized_ip:
            box["ip"] = sanitized_ip
            updated = True

    letters_payload = data.get("letters")
    if isinstance(letters_payload, dict):
        for day, values in letters_payload.items():
            if day in DAYS:
                normalized, error = _collect_letter_update(day, values, 'JSON-Feld "letters"')
                if error:
                    return jsonify({"status": "error", "message": error}), 400
                if normalized is not None:
                    pending_letter_updates[day] = normalized

    for day in DAYS:
        legacy_letter_value = data.get(day)
        if isinstance(legacy_letter_value, str):
            normalized, error = _collect_letter_update(day, legacy_letter_value, f'Legacy-Feld "{day}"')
            if error:
                return jsonify({"status": "error", "message": error}), 400
            if normalized is not None:
                pending_letter_updates[day] = normalized

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

    color_modes_payload = data.get("colorModes")
    if not isinstance(color_modes_payload, dict):
        color_modes_payload = data.get("color_modes")
    if isinstance(color_modes_payload, dict):
        for day, values in color_modes_payload.items():
            if day in DAYS:
                normalized = _normalize_color_mode_list(values)
                if color_modes_matrix.get(day) != normalized:
                    color_modes_matrix[day] = normalized
                    box["colorModes"] = color_modes_matrix
                    updated = True

    color_palette_masks_payload = data.get("colorPaletteMasks")
    if not isinstance(color_palette_masks_payload, dict):
        color_palette_masks_payload = data.get("color_palette_masks")
    if isinstance(color_palette_masks_payload, dict):
        for day, values in color_palette_masks_payload.items():
            if day in DAYS:
                normalized = _normalize_color_palette_mask_list(values)
                if color_palette_masks_matrix.get(day) != normalized:
                    color_palette_masks_matrix[day] = normalized
                    box["colorPaletteMasks"] = color_palette_masks_matrix
                    updated = True

    for day in DAYS:
        if f"delay_{day}" in data:
            normalized = _normalize_delay_list([data.get(f"delay_{day}")])
            if box["delays"].get(day) != normalized:
                box["delays"][day] = normalized
                updated = True

    day = data.get("day")
    trigger_index = data.get("triggerIndex")
    apply_all_triggers = bool(data.get("applyAllTriggers"))
    try:
        trigger_index = int(trigger_index)
    except (TypeError, ValueError):
        trigger_index = None

    if day in DAYS and isinstance(trigger_index, int) and 0 <= trigger_index < TRIGGER_SLOTS:
        target_indices = range(TRIGGER_SLOTS) if apply_all_triggers else [trigger_index]
        if "letter" in data and isinstance(data.get("letter"), str):
            sanitized_letter = sanitize_letter(data["letter"])
            if not sanitized_letter:
                return (
                    jsonify(
                        {
                            "status": "error",
                            "message": f"Ungültiges Zeichen/Symbol für {day} (Trigger {trigger_index + 1}, Quelle: Feld \"letter\").",
                        }
                    ),
                    400,
                )
            current_letters = _current_letters_snapshot(day)
            new_letters = current_letters[:]
            if any(new_letters[index] != sanitized_letter for index in target_indices):
                new_letters = current_letters[:]
                for index in target_indices:
                    new_letters[index] = sanitized_letter
                pending_letter_updates[day] = new_letters
        if "color" in data and isinstance(data.get("color"), str):
            color_value = _sanitize_color(data["color"])
            if any(box["colors"][day][index] != color_value for index in target_indices):
                for index in target_indices:
                    box["colors"][day][index] = color_value
                updated = True
        if "delay" in data:
            delay_value = _coerce_delay_value(data.get("delay"))
            if any(box["delays"][day][index] != delay_value for index in target_indices):
                for index in target_indices:
                    box["delays"][day][index] = delay_value
                updated = True
        if "colorMode" in data:
            color_mode_value = _sanitize_color_mode(data.get("colorMode"))
            if any(color_modes_matrix[day][index] != color_mode_value for index in target_indices):
                for index in target_indices:
                    color_modes_matrix[day][index] = color_mode_value
                box["colorModes"] = color_modes_matrix
                updated = True
        if "colorPaletteMask" in data:
            palette_mask_value = _sanitize_color_palette_mask(data.get("colorPaletteMask"))
            if any(color_palette_masks_matrix[day][index] != palette_mask_value for index in target_indices):
                for index in target_indices:
                    color_palette_masks_matrix[day][index] = palette_mask_value
                box["colorPaletteMasks"] = color_palette_masks_matrix
                updated = True

    if pending_letter_updates:
        for day, new_letters in pending_letter_updates.items():
            if box["letters"].get(day) != new_letters:
                box["letters"][day] = new_letters
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
    global CONFIG_FILE

    original_config_path = CONFIG_FILE
    original_config = load_config()
    backup_config = copy.deepcopy(original_config)

    temp_dir = tempfile.mkdtemp(prefix="reload_all-")
    temp_config_path = os.path.join(temp_dir, "config.json")

    working_config = copy.deepcopy(backup_config)
    working_config["boxen"] = {}
    working_config["boxOrder"] = []

    try:
        CONFIG_FILE = temp_config_path
        save_config(working_config)

        try:
            devices = get_connected_devices()
        except RedirectResponseError as exc:
            CONFIG_FILE = original_config_path
            save_config(backup_config)
            return _redirect_error_response(exc)
        except Exception as exc:
            app.logger.exception("Fehler beim vollständigen Scan")
            CONFIG_FILE = original_config_path
            save_config(backup_config)
            return (
                jsonify(
                    {
                        "status": "❌ Scan fehlgeschlagen",
                        "details": str(exc),
                    }
                ),
                500,
            )

        if not devices:
            CONFIG_FILE = original_config_path
            save_config(backup_config)
            return (
                jsonify({"status": "❌ Keine Geräte gefunden"}),
                503,
            )

        final_config = load_config()
        CONFIG_FILE = original_config_path
        save_config(final_config)
    finally:
        CONFIG_FILE = original_config_path
        try:
            shutil.rmtree(temp_dir)
        except OSError:
            pass

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
        r = requests.get(f"http://{ip}/", headers=box_manager_headers(), timeout=3, allow_redirects=False)
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
    remote_color_modes, remote_color_palette_masks = extract_box_color_settings_from_soup(soup)
    try:
        remote_delays = fetch_trigger_delays(ip)
    except RedirectResponseError as exc:
        return _redirect_error_response(exc)

    stored_letters = {day: list(box["letters"][day]) for day in DAYS}
    stored_colors = {day: list(box["colors"][day]) for day in DAYS}
    stored_delays = {day: [_coerce_delay_value(value) for value in box["delays"][day]] for day in DAYS}
    stored_color_modes = _box_color_modes(box)
    stored_color_palette_masks = _box_color_palette_masks(box)
    uses_custom_color_modes = (
        stored_color_modes != _default_color_mode_matrix()
        or stored_color_palette_masks != _default_color_palette_mask_matrix()
    )

    if (
        remote_letters == stored_letters
        and remote_colors == stored_colors
        and remote_color_modes == stored_color_modes
        and remote_color_palette_masks == stored_color_palette_masks
        and (remote_delays is not None and remote_delays == stored_delays)
    ):
        return jsonify({"status": "⏭️ Bereits aktuell"})

    payload = {
        "letters": stored_letters,
        "colors": stored_colors,
        "delays": stored_delays,
    }
    if uses_custom_color_modes:
        payload["color_modes"] = stored_color_modes
        payload["color_palette_masks"] = stored_color_palette_masks

    try:
        r = requests.post(
            f"http://{ip}/updateAllLetters",
            json=payload,
            headers=box_manager_headers(),
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

@app.route("/transfer_symbol", methods=["POST"])
def transfer_symbol():
    payload = request.get_json(silent=True) or {}
    raw_hostname = payload.get("hostname")
    symbol = str(payload.get("char") or payload.get("slot", "")).strip()
    bitmap = str(payload.get("bitmap", "")).strip()
    enabled = bool(payload.get("enabled", True))

    if not raw_hostname:
        return "Hostname fehlt", 400
    if len(symbol) != 1 or symbol not in "ABCDEFGHIJKLMNOPQRSTUVWXYZ#~&?01234567":
        return "Ungültiges Zeichen/Symbol", 400
    if not _SYMBOL_BITMAP_HEX_PATTERN.fullmatch(bitmap):
        return "Bitmap muss 256 Hex-Zeichen enthalten", 400

    hostname = sanitize_hostname(raw_hostname)
    config = load_config()
    box = config["boxen"].get(hostname) or config["boxen"].get(raw_hostname)
    if not box:
        return "Box unbekannt", 404

    ip = sanitize_ipv4(box.get("ip"))
    if ip == SAFE_IP_PLACEHOLDER:
        return "IP unbekannt", 400

    try:
        response = requests.post(
            f"http://{ip}/api/symbol-bitmap",
            headers=box_manager_headers(),
            data={
                "char": symbol,
                "enabled": "1" if enabled else "0",
                "bitmap": bitmap,
            },
            timeout=3,
            allow_redirects=False,
        )
        if not response.ok and symbol in "01234567":
            response = requests.post(
                f"http://{ip}/api/custom-symbol",
                headers=box_manager_headers(),
                data={
                    "slot": symbol,
                    "enabled": "1" if enabled else "0",
                    "bitmap": bitmap,
                },
                timeout=3,
                allow_redirects=False,
            )
    except requests.RequestException:
        return "Box nicht erreichbar", 503

    if (
        getattr(response, "is_redirect", False)
        or getattr(response, "is_permanent_redirect", False)
        or 300 <= getattr(response, "status_code", 0) < 400
    ):
        return f"Unerwartete Weiterleitung HTTP {response.status_code}", 502
    if not response.ok:
        return response.text or f"Fehler HTTP {response.status_code}", response.status_code

    return "Symbol übertragen", 200

@app.route("/shutdown", methods=["POST"])
def shutdown():
    _execute_poweroff()
    return jsonify({"status": "OK"}), 200


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default=SERVER_HOST)
    parser.add_argument("--port", type=int, default=SERVER_PORT)
    args = parser.parse_args()
    app.run(host=args.host, port=args.port)
