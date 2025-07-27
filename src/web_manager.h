#ifndef WEB_MANAGER_H
#define WEB_MANAGER_H

#include "config.h"
#include "trigger_handler.h"
#include "rtc_manager.h"
#include <ESPAsyncWebServer.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <stdlib.h>

const char scriptJS[] PROGMEM = R"rawliteral(
    // 🕒 Aktuelle Uhrzeit abrufen
    function fetchRTC() {
        fetch('/getTime')
            .then(response => response.text())
            .then(time => {
                document.getElementById('rtcTime').innerText = time;
            })
            .catch(error => console.error('❌ Fehler:', error));
    }

    // 📝 Freien RAM abrufen
    function fetchMemory() {
        fetch('/memory')
            .then(response => response.text())
            .then(memory => {
                document.getElementById('memoryUsage').innerText = memory + ' bytes';
            })
            .catch(error => console.error('❌ Fehler:', error));
    }

    // 🕒 RTC-Zeit setzen
    function setRTC() {
        let form = new FormData(document.getElementById('rtcForm'));
        fetch('/setTime', { method: 'POST', body: form })
            .then(response => response.text())
            .then(alert)
            .catch(error => alert('❌ Fehler: ' + error));
    }

    // 🌐 Zeit per NTP synchronisieren
    function syncNTP() {
        fetch('/syncNTP')
            .then(response => response.text())
            .then(alert)
            .catch(error => alert('❌ Fehler: ' + error));
    }

    // 🔔 Buchstaben-Trigger über Webinterface
    function triggerLetter() {
        fetch('/triggerLetter')
            .then(response => response.text())
            .then(alert)
            .catch(error => alert('❌ Fehler: ' + error));
    }

    // 💾 WiFi-Daten speichern
    function saveWiFi() {
        let form = new FormData(document.getElementById('wifiForm'));
        fetch('/updateWiFi', { method: 'POST', body: form })
            .then(response => response.text())
            .then(alert)
            .catch(error => alert('❌ Fehler: ' + error));
    }

    // 💾 Anzeige-Einstellungen speichern
    function saveDisplaySettings() {
        let form = new FormData(document.getElementById('displayForm'));
        let autoModeChecked = document.getElementById('auto_mode').checked;
        form.set('auto_mode', autoModeChecked ? 'on' : 'off');
        fetch('/updateDisplaySettings', { method: 'POST', body: form })
            .then(response => response.text())
            .then(alert)
            .catch(error => alert('❌ Fehler: ' + error));
    }

    // 💾 Alle Buchstaben & Farben speichern
    function saveAllLetters() {
        let formData = new FormData(document.getElementById('lettersForm'));
        fetch('/updateAllLetters', { method: 'POST', body: formData })
            .then(response => response.text())
            .then(alert)
            .catch(error => alert('❌ Fehler: ' + error));
    }

    // 🚀 Automatische Aktualisierung der Uhrzeit
    let rtcInterval;
    let memoryInterval;

    function startRTCUpdates() {
        rtcInterval = setInterval(fetchRTC, 5000); // Alle 5 Sekunden aktualisieren
        memoryInterval = setInterval(fetchMemory, 5000);
    }

    function stopRTCUpdates() {
        clearInterval(rtcInterval);
        clearInterval(memoryInterval);
    }

    fetchRTC();
    fetchMemory();
    startRTCUpdates();

    const dateInput = document.querySelector("input[name='date']");
    const timeInput = document.querySelector("input[name='time']");

    dateInput.addEventListener('focus', stopRTCUpdates);
    dateInput.addEventListener('blur', startRTCUpdates);
    timeInput.addEventListener('focus', stopRTCUpdates);
    timeInput.addEventListener('blur', startRTCUpdates);
)rawliteral";

// **Webserver Initialisierung**
void setupWebServer() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = "<h1>Märchen Einstellungen</h1>";

        // **WiFi-Einstellungen**
        html += "<h2>WiFi Konfiguration</h2>";
        html += "<form id='wifiForm'>";
        html += "SSID: <input type='text' name='ssid' value='" + String(wifi_ssid) + "'><br>";
        html += "Passwort: <input type='password' name='password'><br>";
        html += "Hostname: <input type='text' name='hostname' value='" + String(hostname) + "'><br>";
        html += "<button type='button' onclick='saveWiFi()'>Speichern</button>";
        html += "</form>";

        // **Anzeige-Einstellungen**
        html += "<h2>Anzeige-Einstellungen</h2>";
        html += "<form id='displayForm'>";
        html += "Helligkeit: <input type='number' name='brightness' min='1' max='255' value='" + String(display_brightness) + "'><br>";
        html += "Buchstaben-Anzeigezeit (Sekunden): <input type='number' name='letter_time' min='1' max='60' value='" + String(letter_display_time) + "'><br>";
        html += "Trigger 1 Verzögerung (Sekunden): <input type='number' name='trigger_delay_1' min='0' max='600' value='" + String(letter_trigger_delay_1) + "'><br>";
        html += "Trigger 2 Verzögerung (Sekunden): <input type='number' name='trigger_delay_2' min='0' max='600' value='" + String(letter_trigger_delay_2) + "'><br>";
        html += "Trigger 3 Verzögerung (Sekunden): <input type='number' name='trigger_delay_3' min='0' max='600' value='" + String(letter_trigger_delay_3) + "'><br>";
        html += "Automodus Intervall (Sekunden): <input type='number' name='auto_interval' min='30' max='600' value='" + String(letter_auto_display_interval) + "'><br>";
        html += "<label><input type='checkbox' id='auto_mode' name='auto_mode' " + String(autoDisplayMode ? "checked='checked'" : "") + "> Automodus aktivieren</label>";
        html += "<br><button type='button' onclick='saveDisplaySettings()'>Speichern</button>";
        html += "</form>";

        // **RTC-Zeit anzeigen & ändern**
        html += "<h2>Datum & Uhrzeit setzen</h2>";
        html += "<p>Aktuelle Zeit: <span id='rtcTime'>Laden...</span></p>";
        html += "<p>Freier RAM: <span id='memoryUsage'>Laden...</span></p>";
        html += "<form id='rtcForm'>";
        html += "Datum (YYYY-MM-DD): <input type='date' name='date'><br>";
        html += "Uhrzeit (HH:MM:SS): <input type='time' name='time' step='1'><br>";
        html += "<button type='button' onclick='setRTC()'>Speichern</button>";
        html += "</form>";
        html += "<button type='button' onclick='syncNTP()'>Zeit mit NTP synchronisieren</button>";

        // **Buchstabenauswahl pro Wochentag**
        html += "<h2>Buchstaben pro Wochentag</h2>";
        html += "<form id='lettersForm'>";
        html += "<table border='1' style='width:100%; text-align:center;'>";
        html += "<tr>";
        for (int i = 0; i < 7; i++) {
            html += "<th>" + String(daysOfTheWeek[i]) + "</th>";
        }
        html += "</tr><tr>";
        for (int i = 0; i < 7; i++) {
            html += "<td>";
            html += "<select id='letter" + String(i) + "' name='letter" + String(i) + "'>";
            for (char c = 'A'; c <= 'Z'; c++) {
                html += "<option value='" + String(c) + "' " + (dailyLetters[i] == c ? "selected" : "") + ">" + String(c) + "</option>";
            }
            html += "<option value='*' " + String(dailyLetters[i] == '*' ? "selected" : "") + ">Sun+Rad</option>";
            html += "<option value='#' " + String(dailyLetters[i] == '#' ? "selected" : "") + ">Sun</option>";
            html += "<option value='~' " + String(dailyLetters[i] == '~' ? "selected" : "") + ">WIFI</option>";
            html += "<option value='&' " + String(dailyLetters[i] == '&' ? "selected" : "") + ">Rad</option>";
            html += "<option value='?' " + String(dailyLetters[i] == '?' ? "selected" : "") + ">Riddler</option>";
            html += "</select>";
            html += "<br><input type='color' id='color" + String(i) + "' name='color" + String(i) + "' value='" + String(dailyLetterColors[i]) + "'>";
            html += "<br><button type='button' onclick='displayLetter(\"" + String(dailyLetters[i]) + "\")'>Anzeigen</button>";
            html += "</td>";
        }
        html += "</tr></table>";
        html += "<br><button type='button' onclick='saveAllLetters()'>Alle speichern</button>";
        html += "</form>";

        // **Manuellen Trigger starten**
        html += "<h2>Manueller Trigger</h2>";
        html += "<button type='button' onclick='triggerLetter()'>Buchstaben anzeigen</button>";

        // **JavaScript-Datei einbinden**
        html += "<script src='/script.js'></script>";
        request->send(200, "text/html", html);
    });

server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/javascript", scriptJS);
});

// **WiFi-Daten speichern**
server.on("/updateWiFi", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("ssid", true) && request->hasParam("hostname", true)) {
        strncpy(wifi_ssid, request->getParam("ssid", true)->value().c_str(), sizeof(wifi_ssid));
        strncpy(hostname, request->getParam("hostname", true)->value().c_str(), sizeof(hostname));

        // Passwort nur aktualisieren, wenn ein neuer Wert eingegeben wurde!
        if (request->hasParam("password", true) && request->getParam("password", true)->value() != "") {
            strncpy(wifi_password, request->getParam("password", true)->value().c_str(), sizeof(wifi_password));
        }

        saveConfig();  // Speichert ins EEPROM
        request->send(200, "text/plain", "✅ WiFi-Einstellungen gespeichert!");
    } else {
        request->send(400, "text/plain", "❌ Fehler: Fehlende Parameter!");
    }
});

    // **Speichern der Display-Einstellungen**
    server.on("/updateDisplaySettings", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("brightness", true) && 
            request->hasParam("letter_time", true) &&
            request->hasParam("trigger_delay_1", true) &&
            request->hasParam("trigger_delay_2", true) &&
            request->hasParam("trigger_delay_3", true) &&
            request->hasParam("auto_interval", true)) {

            display_brightness = request->getParam("brightness", true)->value().toInt();
            letter_display_time = strtoul(request->getParam("letter_time", true)->value().c_str(), nullptr, 10);
            letter_trigger_delay_1 = strtoul(request->getParam("trigger_delay_1", true)->value().c_str(), nullptr, 10);
            letter_trigger_delay_2 = strtoul(request->getParam("trigger_delay_2", true)->value().c_str(), nullptr, 10);
            letter_trigger_delay_3 = strtoul(request->getParam("trigger_delay_3", true)->value().c_str(), nullptr, 10);
            letter_auto_display_interval = strtoul(request->getParam("auto_interval", true)->value().c_str(), nullptr, 10);

            String autoModeValue = request->hasParam("auto_mode", true) ? request->getParam("auto_mode", true)->value() : "off";
            autoDisplayMode = (autoModeValue == "on");

            saveConfig();
            request->send(200, "text/plain", "Anzeigeeinstellungen gespeichert!");
        } else {
            request->send(400, "text/plain", "Fehlende Parameter!");
        }
    });

// **Buchstaben & Farben speichern**
server.on("/updateAllLetters", HTTP_POST, [](AsyncWebServerRequest *request) {
    bool success = true;
    for (int i = 0; i < 7; i++) {
        String letterParam = "letter" + String(i);
        String colorParam = "color" + String(i);

        if (request->hasParam(letterParam, true) && request->hasParam(colorParam, true)) {
            dailyLetters[i] = request->getParam(letterParam, true)->value()[0];
            strncpy(dailyLetterColors[i], request->getParam(colorParam, true)->value().c_str(), sizeof(dailyLetterColors[i]));
        } else {
            success = false;
        }
    }

    if (success) {
        saveConfig();  // Speichert ins EEPROM
        request->send(200, "text/plain", "✅ Buchstaben & Farben gespeichert!");
    } else {
        request->send(400, "text/plain", "❌ Fehler: Nicht alle Werte empfangen!");
    }
});

    // **Anzeige eines Buchstabens**
    server.on("/displayLetter", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("char")) {
            String letter = request->getParam("char")->value();
            displayLetter(letter[0]);  // Buchstaben anzeigen
            request->send(200, "text/plain", "Buchstabe " + letter + " angezeigt!");
        } else {
            request->send(400, "text/plain", "Fehlender Parameter!");
        }
    });

    server.on("/getTime", HTTP_GET, [](AsyncWebServerRequest *request) {
    String currentTime = getRTCTime();
    request->send(200, "text/plain", currentTime);
});

    server.on("/setTime", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("date", true) && request->hasParam("time", true)) {
            String date = request->getParam("date", true)->value();
            String time = request->getParam("time", true)->value();
            setRTCFromWeb(date, time);
            request->send(200, "text/plain", "✅ Uhrzeit erfolgreich gesetzt!");
        } else {
            request->send(400, "text/plain", "❌ Fehler: Datum oder Zeit fehlt!");
        }
    });

    server.on("/syncNTP", HTTP_GET, [](AsyncWebServerRequest *request) {
        syncTimeWithNTP();
        request->send(200, "text/plain", "NTP Synchronisierung ausgeführt");
    });

    server.on("/memory", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", String(ESP.getFreeHeap()));
    });

    server.begin();
}

#endif
