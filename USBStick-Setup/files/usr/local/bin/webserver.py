#!/usr/bin/env python3
from flask import Flask, jsonify, request
from bs4 import BeautifulSoup
import os, json, subprocess, requests

DAYS = ["mo", "di", "mi", "do", "fr", "sa", "so"]
TRIGGER_SLOTS = 3
DEFAULT_COLOR = "#ffffff"

app = Flask(__name__)
LEASE_FILE = "/var/lib/misc/dnsmasq.leases"
CONFIG_FILE = "/mnt/persist/boxen_config/boxen_config.json"

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

        if remove_legacy:
            if day in box:
                del box[day]
                changed = True
            legacy_color_key = f"color_{day}"
            if legacy_color_key in box:
                del box[legacy_color_key]
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

    for day_index, day in enumerate(DAYS):
        for slot in range(TRIGGER_SLOTS):
            select = soup.find("select", {"name": f"letter{day_index}_{slot}"})
            if select is None and slot == 0:
                select = soup.find("select", {"name": f"letter{day_index}"})

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

            color_input = soup.find("input", {"name": f"color{day_index}_{slot}"})
            if color_input is None and slot == 0:
                color_input = soup.find("input", {"name": f"color{day_index}"})

            color_value = DEFAULT_COLOR
            if color_input and color_input.has_attr("value") and color_input["value"]:
                color_value = color_input["value"]
            colors[day][slot] = color_value

    return letters, colors

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
        if r.ok and "name='hostname'" in r.text:
            start = r.text.find("name='hostname'")
            val_start = r.text.find("value='", start) + 7
            val_end = r.text.find("'", val_start)
            return r.text[val_start:val_end]
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
        letters, colors = extract_box_state_from_soup(soup)
        box = {
            "ip": ip,
            "letters": letters,
            "colors": colors,
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
    remote_letters, remote_colors = extract_box_state_from_soup(soup)

    stored_letters = {day: list(box["letters"][day]) for day in DAYS}
    stored_colors = {day: list(box["colors"][day]) for day in DAYS}

    if remote_letters == stored_letters and remote_colors == stored_colors:
        return jsonify({"status": "⏭️ Bereits aktuell"})

    payload = {
        "letters": stored_letters,
        "colors": stored_colors,
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
    os.system("poweroff")
    return "OK", 200

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=8080)
