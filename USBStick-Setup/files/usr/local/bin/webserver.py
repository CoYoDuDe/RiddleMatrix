#!/usr/bin/env python3
from flask import Flask, jsonify, request
from bs4 import BeautifulSoup
import os, json, subprocess, requests

app = Flask(__name__)
LEASE_FILE = "/var/lib/misc/dnsmasq.leases"
CONFIG_FILE = "/mnt/persist/boxen_config/boxen_config.json"

if not os.path.exists(CONFIG_FILE):
    os.makedirs(os.path.dirname(CONFIG_FILE), exist_ok=True)
    with open(CONFIG_FILE, "w") as f:
        json.dump({"boxen": {}, "boxOrder": []}, f)

def load_config():
    with open(CONFIG_FILE, "r") as f:
        return json.load(f)

def save_config(data):
    with open(CONFIG_FILE, "w") as f:
        json.dump(data, f, indent=4)

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
        config["boxen"][identifier]["ip"] = ip
        save_config(config)
        return

    try:
        r = requests.get(f"http://{ip}/", timeout=3)
        if not r.ok:
            return
        soup = BeautifulSoup(r.text, "html.parser")
        box = {"ip": ip}
        wochentage = ["mo", "di", "mi", "do", "fr", "sa", "so"]
        for i, tag in enumerate(wochentage):
            letter = soup.find("select", {"name": f"letter{i}"})
            if letter:
                selected = letter.find("option", selected=True)
                if not selected:
                    selected = letter.find("option")
                if selected:
                    box[tag] = selected.get("value", "")
            color = soup.find("input", {"name": f"color{i}"})
            if color and color.has_attr("value"):
                box[f"color_{tag}"] = color["value"]
        config["boxen"][identifier] = box
        if identifier not in config["boxOrder"]:
            config["boxOrder"].append(identifier)
        save_config(config)
    except:
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
    data = request.json
    hostname = data.get("hostname")
    if hostname:
        config = load_config()
        if hostname not in config["boxen"]:
            config["boxen"][hostname] = {}
        config["boxen"][hostname].update(data)
        save_config(config)
        return jsonify({"status": "success"})
    return jsonify({"status": "error"}), 400

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
    box = config["boxen"].get(hostname, {})
    ip = box.get("ip")
    if not ip:
        return jsonify({"status": "❌ IP unbekannt"})

    r = requests.get(f"http://{ip}/", timeout=3)
    if not r.ok:
        return jsonify({"status": "❌ Box nicht erreichbar"})

    soup = BeautifulSoup(r.text, "html.parser")
    match = True
    tags = ["mo", "di", "mi", "do", "fr", "sa", "so"]
    for i, tag in enumerate(tags):
        select = soup.find("select", {"name": f"letter{i}"})
        current_letter = ""
        if select:
            selected = select.find("option", selected=True)
            if selected and selected.get("value"):
                current_letter = selected["value"]
        if current_letter != box.get(tag, ""):
            match = False
        color_input = soup.find("input", {"name": f"color{i}"})
        current_color = "#ffffff"
        if color_input and color_input.get("value"):
            current_color = color_input["value"]
        if current_color != box.get(f"color_{tag}", "#ffffff"):
            match = False

    if match:
        return jsonify({"status": "⏭️ Bereits aktuell"})

    payload = {f"letter{i}": box.get(tag, "") for i, tag in enumerate(tags)}
    payload.update({f"color{i}": box.get(f"color_{tag}", "#ffffff") for i, tag in enumerate(tags)})
    r = requests.post(f"http://{ip}/updateAllLetters", data=payload, timeout=3)
    if not r.ok:
        return jsonify({"status": "❌ Fehler bei Übertragung"})
    
    return jsonify({"status": "✅ Übertragen"})

@app.route("/shutdown", methods=["POST"])
def shutdown():
    os.system("poweroff")
    return "OK", 200

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=8080)
