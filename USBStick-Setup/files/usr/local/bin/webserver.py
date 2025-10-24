#!/usr/bin/env python3
from flask import Flask, abort, jsonify, request
from bs4 import BeautifulSoup
import math
import os, json, subprocess, requests
import secrets
from typing import Optional, Tuple
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

app = Flask(__name__)
LEASE_FILE = "/var/lib/misc/dnsmasq.leases"
CONFIG_FILE = "/mnt/persist/boxen_config/boxen_config.json"
PUBLIC_AP_ENV_FILE = os.environ.get("PUBLIC_AP_ENV_FILE", "/etc/usbstick/public_ap.env")
SHUTDOWN_TOKEN_ENV = "SHUTDOWN_TOKEN"
ALLOWED_SHUTDOWN_ADDRESSES = {"127.0.0.1", "::1"}

if not os.path.exists(CONFIG_FILE):
    os.makedirs(os.path.dirname(CONFIG_FILE), exist_ok=True)
    with open(CONFIG_FILE, "w") as f:
        json.dump({"boxen": {}, "boxOrder": []}, f)

def load_config():
    with open(CONFIG_FILE, "r") as f:
        data = json.load(f)
    if migrate_config(data):
        save_config(data)
    return data

def save_config(data):
    with open(CONFIG_FILE, "w") as f:
        json.dump(data, f, indent=4)


def _normalize_letter_list(values, legacy_value=None):
    normalized = ["" for _ in range(TRIGGER_SLOTS)]
    if isinstance(values, list):
        for idx in range(min(TRIGGER_SLOTS, len(values))):
            val = values[idx]
            if isinstance(val, str):
                normalized[idx] = val
            elif val is not None:
                normalized[idx] = str(val)
    elif isinstance(values, dict):
        for key, val in values.items():
            if str(key).isdigit():
                idx = int(str(key))
                if 0 <= idx < TRIGGER_SLOTS:
                    if isinstance(val, str):
                        normalized[idx] = val
                    elif val is not None:
                        normalized[idx] = str(val)
    elif isinstance(values, str):
        normalized[0] = values
    elif values is not None:
        normalized[0] = str(values)

    if isinstance(legacy_value, str) and not normalized[0]:
        normalized[0] = legacy_value
    return normalized


def _normalize_color_list(values, legacy_value=None):
    normalized = [DEFAULT_COLOR for _ in range(TRIGGER_SLOTS)]
    if isinstance(values, list):
        for idx in range(min(TRIGGER_SLOTS, len(values))):
            val = values[idx]
            if isinstance(val, str) and val:
                normalized[idx] = val
    elif isinstance(values, dict):
        for key, val in values.items():
            if str(key).isdigit():
                idx = int(str(key))
                if 0 <= idx < TRIGGER_SLOTS and isinstance(val, str) and val:
                    normalized[idx] = val
    elif isinstance(values, str) and values:
        normalized[0] = values

    if isinstance(legacy_value, str) and (not normalized[0] or normalized[0] == DEFAULT_COLOR):
        normalized[0] = legacy_value
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

    for box in data["boxen"].values():
        if ensure_box_structure(box, remove_legacy=True):
            changed = True

    return changed


def extract_box_state_from_soup(soup):
    letters = {day: ["" for _ in range(TRIGGER_SLOTS)] for day in DAYS}
    colors = {day: [DEFAULT_COLOR for _ in range(TRIGGER_SLOTS)] for day in DAYS}
    delays = {day: [DEFAULT_DELAY for _ in range(TRIGGER_SLOTS)] for day in DAYS}

    def _find_field(tag, base_name, firmware_day_index, slot, fallback_day_index=None):
        name_candidates = []

        def extend_candidates(index_value):
            if slot is not None:
                name_candidates.extend(
                    [
                        f"{base_name}_{index_value}_{slot}",
                        f"{base_name}{index_value}_{slot}",
                        f"{base_name}_{slot}_{index_value}",
                        f"{base_name}_{slot}{index_value}",
                        f"{base_name}{slot}{index_value}",
                    ]
                )
            if slot == 0:
                name_candidates.extend(
                    [
                        f"{base_name}_{index_value}",
                        f"{base_name}{index_value}",
                    ]
                )

        extend_candidates(firmware_day_index)
        if fallback_day_index is not None and fallback_day_index != firmware_day_index:
            extend_candidates(fallback_day_index)

        for candidate in dict.fromkeys(name_candidates):
            field = soup.find(tag, {"name": candidate})
            if field is not None:
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
            letters[day][slot] = value or ""

            color_input = _find_field("input", "color", firmware_day_index, slot, fallback_day_index=day_index)

            color_value = DEFAULT_COLOR
            if color_input and color_input.has_attr("value") and color_input["value"]:
                color_value = color_input["value"]
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
    try:
        response = requests.get(f"http://{ip}/api/trigger-delays", timeout=3)
    except requests.RequestException:
        return None

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
                    ip = parts[2]
                    if subprocess.call(["ping", "-c", "1", "-W", "1", ip], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) == 0:
                        try:
                            r = requests.get(f"http://{ip}/", timeout=3)
                            if not r.ok or "name='hostname'" not in r.text:
                                continue
                        except:
                            continue

                        hostname = get_hostname_from_web(ip)
                        if hostname == "Unbekannt":
                            continue

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

                                config["boxen"][existing_identifier]["ip"] = "0.0.0.0"
                                save_config(config)
                                break

                        devices.append({"ip": ip, "hostname": hostname})

                        if not hostname_exists:
                            learn_box(ip, hostname)
    return devices

def get_hostname_from_web(ip):
    try:
        r = requests.get(f"http://{ip}", timeout=3)
        if r.ok:
            soup = BeautifulSoup(r.text, "html.parser")
            hostname_field = soup.find("input", {"name": "hostname"})
            if hostname_field is not None:
                value = hostname_field.get("value")
                if value is not None:
                    return value
    except:
        pass
    return "Unbekannt"

def learn_box(ip, identifier):
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
        r = requests.get(f"http://{ip}/", timeout=3)
        if not r.ok:
            return
        soup = BeautifulSoup(r.text, "html.parser")
        letters, colors, html_delays = extract_box_state_from_soup(soup)
        delays = fetch_trigger_delays(ip)
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
    except Exception:
        pass

@app.route("/")
def index():
    with open("/usr/local/etc/index.html", "r") as f:
        return f.read()

@app.route("/devices")
def devices():
    connected = get_connected_devices()
    config = load_config()
    return jsonify({"boxen": config["boxen"], "connected": connected, "boxOrder": config["boxOrder"]})

@app.route("/update_box", methods=["POST"])
def update_box():
    data = request.json or {}
    hostname = data.get("hostname")
    if not hostname:
        return jsonify({"status": "error"}), 400

    config = load_config()
    box = config["boxen"].setdefault(hostname, {"ip": "0.0.0.0"})
    changed = ensure_box_structure(box, remove_legacy=True)

    updated = False

    if "ip" in data and isinstance(data.get("ip"), str):
        if box.get("ip") != data["ip"]:
            box["ip"] = data["ip"]
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
            letter_value = data["letter"]
            if box["letters"][day][trigger_index] != letter_value:
                box["letters"][day][trigger_index] = letter_value
                updated = True
        if "color" in data and isinstance(data.get("color"), str):
            color_value = data["color"] or DEFAULT_COLOR
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
    data = request.json
    hostname = data.get("hostname")
    config = load_config()
    if hostname in config["boxen"]:
        del config["boxen"][hostname]
        config["boxOrder"] = [h for h in config["boxOrder"] if h != hostname]
        save_config(config)
    return jsonify({"status": "removed"})

@app.route("/update_box_order", methods=["POST"])
def update_box_order():
    data = request.json
    boxOrder = data.get("boxOrder")
    if boxOrder and isinstance(boxOrder, list):
        config = load_config()
        config["boxOrder"] = boxOrder
        save_config(config)
        return jsonify({"status": "success"})
    return jsonify({"status": "error"}), 400

@app.route("/reload_all", methods=["POST"])
def reload_all():
    config = load_config()
    config["boxen"] = {}
    config["boxOrder"] = []
    save_config(config)
    get_connected_devices()
    return jsonify({"status": "reloaded"})

@app.route("/transfer_box")
def transfer_box():
    hostname = request.args.get("hostname")
    config = load_config()
    box = config["boxen"].get(hostname)
    if not box:
        return jsonify({"status": "❌ Box unbekannt"})

    changed = ensure_box_structure(box, remove_legacy=True)
    if changed:
        save_config(config)

    ip = box.get("ip")
    if not ip:
        return jsonify({"status": "❌ IP unbekannt"})

    try:
        r = requests.get(f"http://{ip}/", timeout=3)
    except requests.RequestException:
        return jsonify({"status": "❌ Box nicht erreichbar"})
    if not r.ok:
        return jsonify({"status": "❌ Box nicht erreichbar"})

    soup = BeautifulSoup(r.text, "html.parser")
    remote_letters, remote_colors, _ = extract_box_state_from_soup(soup)
    remote_delays = fetch_trigger_delays(ip)

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
        r = requests.post(f"http://{ip}/updateAllLetters", json=payload, timeout=3)
    except requests.RequestException:
        return jsonify({"status": "❌ Fehler bei Übertragung"})
    if not r.ok:
        return jsonify({"status": "❌ Fehler bei Übertragung"})

    return jsonify({"status": "✅ Übertragen"})

@app.route("/shutdown", methods=["POST"])
def shutdown():
    remote_addr = request.remote_addr or "<unbekannt>"
    forwarded_for = request.headers.get("X-Forwarded-For", "")
    client_addr = _resolve_client_address(remote_addr, forwarded_for)
    token = request.headers.get("X-Api-Key", "")

    authorized, reason = _is_shutdown_authorized(client_addr, token)
    if not authorized:
        app.logger.warning(
            "Shutdown-Anfrage verweigert (%s): client=%s, remote_addr=%s, xff=%s",
            reason,
            client_addr,
            remote_addr,
            forwarded_for or "-",
        )
        abort(403)

    app.logger.info(
        "Shutdown-Anfrage akzeptiert (%s): client=%s, remote_addr=%s, xff=%s",
        reason,
        client_addr,
        remote_addr,
        forwarded_for or "-",
    )
    os.system("poweroff")
    return jsonify({"status": "OK"}), 200


def _is_shutdown_authorized(remote_addr: str, provided_token: str) -> Tuple[bool, str]:
    if remote_addr in ALLOWED_SHUTDOWN_ADDRESSES:
        return True, "Loopback"

    expected_token = _load_shutdown_token()
    if not expected_token:
        return False, "kein Token konfiguriert"

    if not provided_token:
        return False, "kein Token übermittelt"

    try:
        if secrets.compare_digest(expected_token, provided_token):
            return True, "Token akzeptiert"
    except Exception:
        return False, "Token-Vergleich fehlgeschlagen"

    return False, "Token ungültig"


def _resolve_client_address(remote_addr: str, forwarded_for_header: str) -> str:
    if remote_addr in ALLOWED_SHUTDOWN_ADDRESSES and forwarded_for_header:
        forwarded_addrs = [entry.strip() for entry in forwarded_for_header.split(",") if entry.strip()]
        if forwarded_addrs:
            # Trust the last hop in the chain so spoofed entries injected by
            # clients at the beginning of the header do not bypass checks.
            return forwarded_addrs[-1]
    return remote_addr


@lru_cache(maxsize=1)
def _load_shutdown_token() -> Optional[str]:
    token = os.environ.get(SHUTDOWN_TOKEN_ENV, "").strip()
    if token:
        return token

    if not PUBLIC_AP_ENV_FILE:
        return None

    try:
        with open(PUBLIC_AP_ENV_FILE, "r") as f:
            for line in f:
                stripped = line.strip()
                if not stripped or stripped.startswith("#"):
                    continue
                if "=" not in stripped:
                    continue
                key, value = stripped.split("=", 1)
                if key.strip() == SHUTDOWN_TOKEN_ENV:
                    cleaned = value.strip().strip('"').strip("'")
                    return cleaned or None
    except OSError:
        pass

    return None


def reload_shutdown_token_cache():
    """Hilfsfunktion für Tests, um den Token neu einzulesen."""

    _load_shutdown_token.cache_clear()

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=8080)
