#!/bin/bash
set -e

echo "🚀 Starte minimale Debian-Einrichtung für Märchen Manager..."

## 📦 Minimale Pakete installieren
echo "📦 Installiere Pakete..."
apt update
apt install -y python3 python3-pip python3-venv dnsmasq hostapd rfkill xserver-xorg-core xinit firefox-esr firmware-linux-nonfree firmware-iwlwifi firmware-realtek firmware-misc-nonfree linux-image-cloud-amd64

## 🐍 Python-Abhängigkeiten in virtueller Umgebung
echo "🐍 Erstelle virtuelle Umgebung und installiere Python-Abhängigkeiten..."
mkdir -p /usr/local/venv
python3 -m venv /usr/local/venv/maerchen
/usr/local/venv/maerchen/bin/pip install flask beautifulsoup4 requests

## 📁 Projektverzeichnis & Dateien
mkdir -p /usr/local/bin /usr/local/etc /mnt/persist/boxen_config /home/kioskuser/.X.d

## 🔍 Dynamische Erkennung der ersten Partition des USB-Sticks
echo "🔍 Suche nach erster Partition des USB-Sticks für Persistenz..."
PERSIST_PART=""
for dev in /dev/sd*; do
    if [ -b "$dev" ] && [ "$(cat /sys/block/${dev##*/}/removable 2>/dev/null)" = "1" ]; then
        for part in ${dev}*[1]; do
            if [ -b "$part" ] && blkid "$part" | grep -q "TYPE=\"ext4\""; then
                PERSIST_PART="$part"
                break
            fi
        done
        [ -n "$PERSIST_PART" ] && break
    fi
done

if [ -z "$PERSIST_PART" ]; then
    echo "⚠️ Warnung: Keine erste ext4-Partition auf USB-Stick gefunden. boxen_config.json wird im RAM gespeichert."
    CONFIG_MOUNT="/mnt/persist"
    mkdir -p /mnt/persist
else
    echo "✅ Gefundene Partition: $PERSIST_PART"
    CONFIG_MOUNT="/mnt/persist"
    mkdir -p /mnt/persist
    # Prüfe, ob die Partition bereits als Root-Dateisystem gemountet ist
    if mount | grep "$PERSIST_PART on / "; then
        echo "ℹ️ $PERSIST_PART ist bereits als Root-Dateisystem gemountet. Verwende /mnt/persist direkt."
    else
        echo "$PERSIST_PART /mnt/persist ext4 defaults,noatime 0 1" >> /etc/fstab
        mount $PERSIST_PART /mnt/persist 2>/dev/null || echo "⚠️ Mount von $PERSIST_PART fehlgeschlagen. Prüfe Partition."
    fi
fi

## 📝 Erstelle webserver.py
echo "📝 Erstelle webserver.py..."
cat > /usr/local/bin/webserver.py <<'EOF'
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
    if os.path.exists(LEASE_FILE):
        with open(LEASE_FILE, "r") as f:
            for line in f:
                parts = line.split()
                if len(parts) >= 3:
                    ip = parts[2]
                    if subprocess.call(["ping", "-c", "1", "-W", "1", ip], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) == 0:
                        hostname = get_hostname_from_web(ip)
                        devices.append({"ip": ip, "hostname": hostname})
                        identifier = hostname if hostname != "Unbekannt" else f"unknown_{ip.replace('.', '_')}"
                        learn_box(ip, identifier)
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
EOF

echo "📝 Erstelle index.html..."
cat > /usr/local/etc/index.html <<'EOF'
<script type="text/javascript">
        var gk_isXlsx = false;
        var gk_xlsxFileLookup = {};
        var gk_fileData = {};
        function filledCell(cell) {
          return cell !== '' && cell != null;
        }
        function loadFileData(filename) {
        if (gk_isXlsx && gk_xlsxFileLookup[filename]) {
            try {
                var workbook = XLSX.read(gk_fileData[filename], { type: 'base64' });
                var firstSheetName = workbook.SheetNames[0];
                var worksheet = workbook.Sheets[firstSheetName];

                // Convert sheet to JSON to filter blank rows
                var jsonData = XLSX.utils.sheet_to_json(worksheet, { header: 1, blankrows: false, defval: '' });
                // Filter out blank rows (rows where all cells are empty, null, or undefined)
                var filteredData = jsonData.filter(row => row.some(filledCell));

                // Heuristic to find the header row by ignoring rows with fewer filled cells than the next row
                var headerRowIndex = jsonData.findIndex((row, index) =>
                  row.filter(filledCell).length >= jsonData[index + 1]?.filter(filledCell).length
                );
                // Fallback
                if (headerRowIndex === -1 || headerRowIndex > 25) {
                  headerRowIndex = 0;
                }

                // Convert filtered JSON back to CSV
                var csv = XLSX.utils.aoa_to_sheet(filteredData.slice(headerRowIndex)); // Create a new sheet from filtered array of arrays
                csv = XLSX.utils.sheet_to_csv(csv, { header: 1 });
                return csv;
            } catch (e) {
                console.error(e);
                return "";
            }
        }
        return gk_fileData[filename] || "";
        }
        </script><!DOCTYPE html>
<html lang="de">
<head>
  <meta charset="UTF-8">
  <title>Boxen verwalten</title>
  <style>
    body { font-family: sans-serif; margin: 0; display: flex; height: 100vh; }
    #sidebar {
      width: 250px; background: #333; color: white; padding: 10px; display: flex;
      flex-direction: column; justify-content: space-between;
    }
    #deviceList { flex-grow: 1; overflow-y: auto; }
    .device { padding: 8px; cursor: pointer; border-bottom: 1px solid #555; }
    .device:hover { background: #444; }
    #shutdown, #setupBtn {
      margin-top: 10px; padding: 10px; font-size: 14px; cursor: pointer;
    }
    #shutdown { background: red; color: white; }
    #setupBtn { background: #555; color: white; width: 100%; }
    #content {
      flex-grow: 1; background: #f4f4f4; padding: 0; overflow-y: auto; position: relative;
    }
    #content iframe {
      width: 100%; height: 100%; border: none;
    }
    .box-card {
      background: #fff; border: 1px solid #ccc; padding: 10px; margin-bottom: 15px;
    }
    .weekday-row {
      display: flex; gap: 8px; justify-content: space-between; flex-wrap: wrap;
    }
    .weekday-col {
      display: flex; flex-direction: column; align-items: center; min-width: 80px;
    }
    .weekday-col select, .weekday-col input[type="color"], .weekday-col input[type="text"] {
      margin-top: 4px; width: 100%; box-sizing: border-box;
    }
    .word-input {
      margin-bottom: 10px; padding: 5px; width: 100%; max-width: 300px;
      transition: border-color 0.3s;
    }
    .word-input.pending { border: 2px solid red; }
    .word-input.saved { border: 2px solid green; }
    .action-buttons button {
      padding: 10px; margin-right: 10px; font-size: 14px;
    }
    .status { font-weight: bold; }
    .statusEntry { margin-bottom: 5px; }
    .order-buttons button {
      padding: 5px; margin-left: 5px; font-size: 12px; cursor: pointer;
    }
    .box-order {
      font-weight: bold; margin-right: 10px;
    }
  </style>
</head>
<body onload="init()">
<div id="sidebar">
  <div>
    <h3>Märchen</h3>
    <button id="setupBtn" onclick="showSetup()">Boxen verwalten</button>
    <div id="deviceList">Lade...</div>
  </div>
  <button id="shutdown" onclick="shutdown()">Herunterfahren</button>
</div>
<div id="content"><p>Willkommen im Märchen-Manager</p></div>
<script>
let knownBoxes = {};
let connectedBoxes = [];
let transferQueue = {};
let transferring = false;
let statusArea;
let boxOrder = [];

function init() {
  fetchDevices();
  setInterval(fetchDevices, 1000); // Prüfe alle 1 Sekunde
}

async function fetchDevices() {
  try {
    const res = await fetch("/devices");
    const data = await res.json();
    const newKnownBoxes = data.boxen || {};
    const newConnectedBoxes = data.connected || [];
    const newBoxOrder = data.boxOrder || Object.keys(newKnownBoxes);

    // Prüfe auf Änderungen
    const hasChanges =
      JSON.stringify(knownBoxes) !== JSON.stringify(newKnownBoxes) ||
      JSON.stringify(connectedBoxes) !== JSON.stringify(newConnectedBoxes) ||
      JSON.stringify(boxOrder) !== JSON.stringify(newBoxOrder);

    knownBoxes = newKnownBoxes;
    connectedBoxes = newConnectedBoxes;
    boxOrder = newBoxOrder;

    renderDeviceList();
    if (hasChanges && statusArea && !transferring && !document.activeElement.classList.contains("word-input")) {
      showSetup();
    }
  } catch (e) {
    console.error("Fehler beim Abrufen der Geräte:", e);
    if (statusArea) statusArea.innerHTML = '<div class="statusEntry status">❌ Fehler beim Laden der Boxen.</div>';
  }
}

function renderDeviceList() {
  const list = document.getElementById("deviceList");
  list.innerHTML = connectedBoxes.length ? "" : "Keine verbundenen Boxen";
  connectedBoxes.forEach(({ hostname, ip }) => {
    const div = document.createElement("div");
    div.className = "device";
    div.innerText = `${hostname} (${ip})`;
    div.onclick = () => openBox(ip);
    list.appendChild(div);
  });
}

function openBox(ip) {
  document.getElementById("content").innerHTML = `<iframe src="http://${ip}"></iframe>`;
}

function shutdown() {
  fetch("/shutdown", { method: "POST" });
}

function showSetup() {
  const letterLabels = {"*": "Sun+Rad", "#": "Sun", "~": "WIFI", "&": "Rad", "?": "Riddler"};
  let html = '<h2>Boxen verwalten</h2>';
  const days = ["mo", "di", "mi", "do", "fr", "sa", "so"];
  const dayNames = ["Mo", "Di", "Mi", "Do", "Fr", "Sa", "So"];
  const maxLength = boxOrder.length;

  html += '<div class="box-card"><h3>Wörter für Wochentage</h3><div class="weekday-row">';
  for (let i = 0; i < 7; i++) {
    const currentWord = boxOrder.map(hostname => knownBoxes[hostname]?.[days[i]] || "").join("");
    html += `
      <div class="weekday-col">
        <label>${dayNames[i]}</label>
        <input type="text" class="word-input" placeholder="Wort eingeben (max ${maxLength} Zeichen)" 
          value="${currentWord}" onchange="updateWord('${days[i]}', this.value, ${maxLength}, this)" 
          onkeydown="handleKeydown(event, '${days[i]}', this.value, ${maxLength}, this)">
      </div>`;
  }
  html += '</div></div>';

  boxOrder.forEach((hostname, index) => {
    const box = knownBoxes[hostname] || {};
    html += `<div class="box-card"><h3><span class="box-order">${index + 1}</span>${hostname}</h3><div class="weekday-row">`;
    for (let i = 0; i < 7; i++) {
      const letter = box[days[i]] || "";
      const color = box["color_" + days[i]] || "#ffffff";
      const letterOptions = "ABCDEFGHIJKLMNOPQRSTUVWXYZ*#~&?".split("").map(c =>
        `<option value="${c}" ${c === letter ? "selected" : ""}>${letterLabels[c] || c}</option>`).join("");

      html += `
        <div class="weekday-col">
          <label>${dayNames[i]}</label>
          <select onchange="saveField('${hostname}', '${days[i]}', this.value)">
            ${letterOptions}
          </select>
          <input type="color" value="${color}" onchange="saveField('${hostname}', 'color_${days[i]}', this.value)">
        </div>`;
    }
    html += `</div>
      <div class="order-buttons">
        <button onclick="moveBoxUp('${hostname}')">↑</button>
        <button onclick="moveBoxDown('${hostname}')">↓</button>
        <button onclick="removeBox('${hostname}')">❌ Entfernen</button>
      </div></div>`;
  });

  html += `
  <div class="action-buttons">
    <button onclick="transferAll()" id="transferBtn">✅ Übertragen</button>
    <button onclick="reloadAll()">🔄 Boxen neu lernen</button>
  </div>
  <div id="statusArea"></div>
  `;

  document.getElementById("content").innerHTML = html;
  statusArea = document.getElementById("statusArea");
}

function handleKeydown(event, day, word, maxLength, inputElement) {
  if (event.key === "Enter") {
    updateWord(day, word, maxLength, inputElement);
  }
}

function updateWord(day, word, maxLength, inputElement) {
  inputElement.classList.add("pending");
  word = word.toUpperCase().replace(/[^A-Z*#~&?]/g, '').slice(0, maxLength);
  for (let i = 0; i < boxOrder.length; i++) {
    const letter = i < word.length ? word[i] : "*";
    knownBoxes[boxOrder[i]] = knownBoxes[boxOrder[i]] || {};
    knownBoxes[boxOrder[i]][day] = letter;
    saveField(boxOrder[i], day, letter, false, inputElement);
  }
  showSetup();
}

function saveField(hostname, key, value, updateUI = true, inputElement = null) {
  knownBoxes[hostname] = knownBoxes[hostname] || {};
  knownBoxes[hostname][key] = value;
  fetch("/update_box", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ hostname, [key]: value })
  }).then(response => {
    if (!response.ok) {
      statusArea.innerHTML = '<div class="statusEntry status">❌ Fehler beim Speichern.</div>';
      if (inputElement) inputElement.classList.remove("pending");
    } else {
      if (inputElement) {
        inputElement.classList.remove("pending");
        inputElement.classList.add("saved");
        setTimeout(() => inputElement.classList.remove("saved"), 1000);
        statusArea.innerHTML = '<div class="statusEntry status">✅ Änderung gespeichert.</div>';
      }
      if (updateUI) showSetup();
    }
  }).catch(e => {
    console.error("Fehler beim Speichern:", e);
    statusArea.innerHTML = '<div class="statusEntry status">❌ Fehler beim Speichern.</div>';
    if (inputElement) inputElement.classList.remove("pending");
  });
}

function moveBoxUp(hostname) {
  const index = boxOrder.indexOf(hostname);
  if (index > 0) {
    [boxOrder[index - 1], boxOrder[index]] = [boxOrder[index], boxOrder[index - 1]];
    saveBoxOrder();
  }
}

function moveBoxDown(hostname) {
  const index = boxOrder.indexOf(hostname);
  if (index < boxOrder.length - 1) {
    [boxOrder[index], boxOrder[index + 1]] = [boxOrder[index + 1], boxOrder[index]];
    saveBoxOrder();
  }
}

function saveBoxOrder() {
  fetch("/update_box_order", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ boxOrder })
  }).then(response => {
    if (!response.ok) {
      statusArea.innerHTML = '<div class="statusEntry status">❌ Fehler beim Speichern der Reihenfolge.</div>';
    } else {
      showSetup();
    }
  }).catch(e => {
    console.error("Fehler beim Speichern der Reihenfolge:", e);
    statusArea.innerHTML = '<div class="statusEntry status">❌ Fehler beim Speichern der Reihenfolge.</div>';
  });
}

function removeBox(hostname) {
  fetch("/remove_box", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ hostname })
  }).then(response => {
    if (!response.ok) {
      statusArea.innerHTML = '<div class="statusEntry status">❌ Fehler beim Entfernen der Box.</div>';
    } else {
      boxOrder = boxOrder.filter(h => h !== hostname);
      delete knownBoxes[hostname];
      fetchDevices();
    }
  }).catch(e => {
    console.error("Fehler beim Entfernen:", e);
    statusArea.innerHTML = '<div class="statusEntry status">❌ Fehler beim Entfernen der Box.</div>';
  });
}

async function reloadAll() {
  try {
    const res = await fetch("/reload_all", { method: "POST" });
    const data = await res.json();
    if (data.status === "reloaded") {
      knownBoxes = {};
      boxOrder = [];
      fetchDevices();
      statusArea.innerHTML = '<div class="statusEntry status">✅ Alle Boxen gelöscht, bereit zum Neu-Lernen.</div>';
    } else {
      statusArea.innerHTML = '<div class="statusEntry status">❌ Fehler beim Löschen der Boxen.</div>';
    }
  } catch (e) {
    console.error("Fehler beim Neu-Lernen:", e);
    statusArea.innerHTML = '<div class="statusEntry status">❌ Fehler beim Neu-Lernen.</div>';
  }
}

function transferAll() {
  if (transferring) {
    transferring = false;
    document.getElementById("transferBtn").innerText = "✅ Übertragen";
    return;
  }
  transferring = true;
  document.getElementById("transferBtn").innerText = "⛔ Abbrechen";
  statusArea.innerHTML = "";
  transferQueue = {};
  for (const hostname of boxOrder) {
    transferQueue[hostname] = "🕓 wartet...";
    updateStatus(hostname);
  }
  checkTransferStatus();
}

function updateStatus(hostname) {
  let row = document.getElementById("status-" + hostname);
  if (!row) {
    row = document.createElement("div");
    row.id = "status-" + hostname;
    row.className = "statusEntry";
    statusArea.appendChild(row);
  }
  row.innerText = `${hostname}: ${transferQueue[hostname]}`;
}

function checkTransferStatus() {
  for (const hostname of boxOrder) {
    if (transferQueue[hostname] === "🕓 wartet...") {
      const found = connectedBoxes.find(b => b.hostname === hostname);
      if (found) {
        transferQueue[hostname] = "⏳ überträgt...";
        updateStatus(hostname);
        fetch(`/transfer_box?hostname=${hostname}`)
          .then(r => r.json())
          .then(data => {
            transferQueue[hostname] = data.status;
            updateStatus(hostname);
            checkIfDone();
          })
          .catch(() => {
            transferQueue[hostname] = "❌ Fehler";
            updateStatus(hostname);
            checkIfDone();
          });
      }
    }
  }
}

function checkIfDone() {
  if (!transferring) return;
  const open = Object.values(transferQueue).some(v =>
    v === "🕓 wartet..." || v === "⏳ überträgt...");
  if (!open) {
    transferring = false;
    document.getElementById("transferBtn").innerText = "✅ Übertragen";
    const note = document.createElement("div");
    note.className = "statusEntry status";
    note.innerText = "✅ Alle Boxen wurden verarbeitet.";
    statusArea.appendChild(note);
  }
}
</script>
<script>(function(){function c(){var b=a.contentDocument||a.contentWindow.document;if(b){var d=b.createElement('script');d.innerHTML="window.__CF$cv$params={r:'934015dafd24b01c',t:'MTc0NTI3MjEwNC4wMDAwMDA='};var a=document.createElement('script');a.nonce='';a.src='/cdn-cgi/challenge-platform/scripts/jsd/main.js';document.getElementsByTagName('head')[0].appendChild(a);";b.getElementsByTagName('head')[0].appendChild(d)}}if(document.body){var a=document.createElement('iframe');a.height=1;a.width=1;a.style.position='absolute';a.style.top=0;a.style.left=0;a.style.border='none';a.style.visibility='hidden';document.body.appendChild(a);if('loading'!==document.readyState)c();else if(window.addEventListener)document.addEventListener('DOMContentLoaded',c);else{var e=document.onreadystatechange||function(){};document.onreadystatechange=function(b){e(b);'loading'!==document.readyState&&(document.onreadystatechange=e,c())}}}})();</script></body>
</html>
EOF

echo "📝 Erstelle bootlocal.sh..."
cat > /usr/local/bin/bootlocal.sh <<'EOF'
#!/bin/bash
# Initialisiere WLAN-Interface
rfkill unblock wifi
WIFI_IFACE=$(iw dev | awk '$1=="Interface"{print $2}' | head -n1)
[ -z "$WIFI_IFACE" ] && WIFI_IFACE=wlan0
ifconfig $WIFI_IFACE 192.168.10.1 netmask 255.255.255.0 up

# Erstelle Konfigurationsdateien
cat > /etc/dnsmasq.conf <<EOL
interface=$WIFI_IFACE
bind-interfaces
dhcp-range=192.168.10.100,192.168.10.200,12h
EOL

cat > /etc/hostapd/hostapd.conf <<EOL
interface=$WIFI_IFACE
driver=nl80211
ssid=Traumland_Maerchen
hw_mode=g
channel=6
wpa=2
wpa_passphrase=MaerchenByLothar
EOL

# Stelle sicher, dass DHCP-Leases-Verzeichnis existiert
mkdir -p /var/lib/misc
touch /var/lib/misc/dnsmasq.leases
chmod 777 /var/lib/misc/dnsmasq.leases

# Starte systemd-Services
systemctl start hostapd
systemctl start dnsmasq
systemctl start webserver
EOF

chmod +x /usr/local/bin/bootlocal.sh
chmod +x /usr/local/bin/webserver.py

## 📝 Erstelle webserver.service
echo "📝 Erstelle webserver.service..."
cat > /etc/systemd/system/webserver.service <<'EOF'
[Unit]
Description=Märchen Manager Webserver
After=network.target

[Service]
ExecStart=/usr/local/venv/maerchen/bin/python3 /usr/local/bin/webserver.py
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

systemctl enable webserver.service

## 📁 tmpfs für RAM-basierte Ausführung
echo "🛠️ Konfiguriere tmpfs für RAM-basierte Ausführung..."
echo "tmpfs /usr/local tmpfs defaults,noatime,mode=0755 0 0" >> /etc/fstab
echo "tmpfs /var/lib/misc tmpfs defaults,noatime,mode=0755 0 0" >> /etc/fstab

## 🖥️ Xorg-Konfiguration
echo "📝 Erstelle Xorg-Konfiguration..."
mkdir -p /etc/X11/xorg.conf.d
cat > /etc/X11/xorg.conf.d/10-screen.conf <<'EOF'
Section "Monitor"
    Identifier "Monitor0"
    Option "PreferredMode" "1024x720"
EndSection

Section "Screen"
    Identifier "Screen0"
    Monitor "Monitor0"
    DefaultDepth 24
    SubSection "Display"
        Depth 24
        Modes "1024x720" "800x600" "640x480"
    EndSubSection
EndSection

Section "Device"
    Identifier "Card0"
    Driver "modesetting"
EndSection
EOF

## 🖥️ Vesa-Fallback für Grafik
echo "📝 Erstelle Vesa-Fallback-Konfiguration..."
cat > /etc/X11/xorg.conf.d/20-vesa-fallback.conf <<'EOF'
Section "Device"
    Identifier "Card0"
    Driver "vesa"
EndSection
EOF

## 🧑‍💻 Kiosk-Benutzer + Autostart Firefox
useradd -m kioskuser || true
echo "🖥️ Erstelle Autostart für X und Firefox..."
cat > /home/kioskuser/.xinitrc <<'EOF'
#!/bin/bash
xset -dpms
xset s off
xset s noblank
firefox-esr --kiosk http://localhost:8080
EOF

chmod +x /home/kioskuser/.xinitrc
chown kioskuser:kioskuser /home/kioskuser/.xinitrc

## 🛠️ Autologin auf TTY1 + Autostart X
echo "🛠️ Aktiviere Autologin + X-Start..."
mkdir -p /etc/systemd/system/getty@tty1.service.d
cat > /etc/systemd/system/getty@tty1.service.d/override.conf <<'EOF'
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin kioskuser --noclear %I $TERM
EOF

echo "👆 Starte X automatisch für Benutzer kioskuser..."
echo '[[ -z $DISPLAY && $XDG_VTNR -eq 1 ]] && startx' >> /home/kioskuser/.bash_profile
chown kioskuser:kioskuser /home/kioskuser/.bash_profile

## 🛠️ Bootlocal als Systemd-Service
echo "📝 Erstelle bootlocal.service..."
cat > /etc/systemd/system/bootlocal.service <<'EOF'
[Unit]
Description=Start bootlocal.sh
After=network.target

[Service]
Type=oneshot
ExecStart=/usr/local/bin/bootlocal.sh
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF
systemctl enable bootlocal.service

## 🛠️ Deaktiviere unnötige Dienste
echo "🛠️ Deaktiviere unnötige Dienste..."
systemctl disable bluetooth || true
systemctl disable networking || true
systemctl disable cron || true
systemctl disable avahi-daemon || true
systemctl disable cups || true
systemctl disable ModemManager || true
systemctl disable NetworkManager || true
systemctl disable rsyslog || true
systemctl disable systemd-journal-flush || true

## ✨ Boot-Prozess optimieren
echo "🖥️ Optimiere GRUB für schnelleres Booten..."
sed -i 's/GRUB_TIMEOUT=.*/GRUB_TIMEOUT=0/' /etc/default/grub
sed -i 's/GRUB_CMDLINE_LINUX_DEFAULT=".*"/GRUB_CMDLINE_LINUX_DEFAULT="quiet noplymouth fastboot"/' /etc/default/grub
update-grub

echo "📝 Optional: Passe Boot-Text an (sichtbar vor Login)..."
echo "Märchen Manager" > /etc/issue

echo "🛠️ Optimiere Dateisystem für schnellere I/O..."
sed -i 's/errors=remount-ro/errors=remount-ro,noatime/' /etc/fstab

echo "🛠️ Optimiere Initramfs für schnelleres Booten..."
echo "omit_drivers='bluetooth hid sound'" > /etc/initramfs-tools/conf.d/omit-modules.conf
update-initramfs -u

## 🧼 Abschluss
echo "✅ Einrichtung abgeschlossen. Starte neu mit:"
echo "   reboot"
echo "⚠️ Hinweis: Prüfe Service-Status mit:"
echo "   systemctl status webserver"
echo "   systemctl status hostapd"
echo "   systemctl status dnsmasq"
echo "   systemctl status bootlocal"
echo "Prüfe Boot-Zeit mit 'systemd-analyze'. Persistenz für boxen_config ist auf /mnt/persist eingerichtet."