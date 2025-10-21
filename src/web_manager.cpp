#include "web_manager.h"

namespace {

String getLetterOptionLabel(char letter) {
    switch (letter) {
        case '*':
            return F("Sun+Rad");
        case '#':
            return F("Sun");
        case '~':
            return F("WiFi");
        case '&':
            return F("Rad");
        case '?':
            return F("Riddler");
        default:
            return String(letter);
    }
}

} // namespace

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
    function triggerLetter(triggerIndex) {
        let query = '';
        if (typeof triggerIndex === 'number' && triggerIndex >= 0) {
            query = '?trigger=' + encodeURIComponent(triggerIndex + 1);
        }

        fetch('/triggerLetter' + query)
            .then(response => response.text())
            .then(alert)
            .catch(error => alert('❌ Fehler: ' + error));
    }

    // 👁️ Buchstaben direkt anzeigen
    function displayLetter(triggerIndex, letter) {
        if (typeof letter !== 'string' || letter.length !== 1) {
            console.warn('❌ Ungültiger Buchstabe:', letter);
            alert('❌ Bitte einen einzelnen Buchstaben auswählen.');
            return;
        }

        let url = '/displayLetter?char=' + encodeURIComponent(letter);
        if (typeof triggerIndex === 'number' && triggerIndex >= 0) {
            url += '&trigger=' + encodeURIComponent(triggerIndex + 1);
        }

        fetch(url)
            .then(response => response.text())
            .then(message => {
                console.log('ℹ️ Serverantwort:', message);
                alert(message);
            })
            .catch(error => {
                console.error('❌ Fehler:', error);
                alert('❌ Fehler: ' + error);
            });
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

    if (dateInput) {
        dateInput.addEventListener('focus', stopRTCUpdates);
        dateInput.addEventListener('blur', startRTCUpdates);
    } else {
        console.warn('⚠️ Datumseingabe nicht gefunden, automatische Aktualisierung bleibt aktiv.');
    }

    if (timeInput) {
        timeInput.addEventListener('focus', stopRTCUpdates);
        timeInput.addEventListener('blur', startRTCUpdates);
    } else {
        console.warn('⚠️ Uhrzeiteingabe nicht gefunden, automatische Aktualisierung bleibt aktiv.');
    }
)rawliteral";

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
        html += "<h2>Buchstaben pro Wochentag &amp; Trigger</h2>";
        html += "<form id='lettersForm'>";
        html += "<table border='1' style='width:100%; text-align:center;'>";
        html += "<tr><th>Wochentag</th>";
        for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
            html += "<th>Trigger " + String(trigger + 1) + "</th>";
        }
        html += "</tr>";

        for (size_t day = 0; day < NUM_DAYS; ++day) {
            html += "<tr>";
            html += "<td>" + String(daysOfTheWeek[day]) + "</td>";

            for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
                String selectId = "letter_" + String(trigger) + "_" + String(day);
                String colorId = "color_" + String(trigger) + "_" + String(day);
                html += "<td>";
                html += "<select id='" + selectId + "' name='" + selectId + "'>";
                for (size_t idx = 0; idx < sizeof(availableLetters); ++idx) {
                    char optionChar = availableLetters[idx];
                    String optionLabel = getLetterOptionLabel(optionChar);
                    html += "<option value='" + String(optionChar) + "' ";
                    html += (dailyLetters[trigger][day] == optionChar) ? "selected" : "";
                    html += ">" + optionLabel + "</option>";
                }
                html += "</select>";
                html += "<br><input type='color' id='" + colorId + "' name='" + colorId + "' value='" + String(dailyLetterColors[trigger][day]) + "'>";
                html += "<br><button type='button' onclick='displayLetter(" + String(trigger) + ", document.getElementById(\"" + selectId + "\").value)'>Anzeigen</button>";
                html += "<br><button type='button' onclick='triggerLetter(" + String(trigger) + ")'>Triggern</button>";
                html += "</td>";
            }

            html += "</tr>";
        }

        html += "</table>";
        html += "<br><button type='button' onclick='saveAllLetters()'>Alle speichern</button>";
        html += "</form>";

        // **Manuellen Trigger starten**
        html += "<h2>Manueller Trigger</h2>";
        for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
            html += "<button type='button' style='margin-right:8px;' onclick='triggerLetter(" + String(trigger) + ")'>Trigger " + String(trigger + 1) + " auslösen</button>";
        }

        // **JavaScript-Datei einbinden**
        html += "<script src='/script.js'></script>";
        request->send(200, "text/html", html);
    });

    server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send_P(200, "text/javascript", scriptJS);
    });

    server.on("/updateWiFi", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("ssid", true) && request->hasParam("hostname", true)) {
            strncpy(wifi_ssid, request->getParam("ssid", true)->value().c_str(), sizeof(wifi_ssid));
            strncpy(hostname, request->getParam("hostname", true)->value().c_str(), sizeof(hostname));

            if (request->hasParam("password", true) && request->getParam("password", true)->value() != "") {
                strncpy(wifi_password, request->getParam("password", true)->value().c_str(), sizeof(wifi_password));
            }

            saveConfig();
            request->send(200, "text/plain", "✅ WiFi-Einstellungen gespeichert!");
        } else {
            request->send(400, "text/plain", "❌ Fehler: Fehlende Parameter!");
        }
    });

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

    server.on("/updateAllLetters", HTTP_POST, [](AsyncWebServerRequest *request) {
        bool success = true;

        for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
            for (size_t day = 0; day < NUM_DAYS; ++day) {
                String letterParam = "letter_" + String(trigger) + "_" + String(day);
                String colorParam = "color_" + String(trigger) + "_" + String(day);

                if (request->hasParam(letterParam, true) && request->hasParam(colorParam, true)) {
                    String letterValue = request->getParam(letterParam, true)->value();
                    if (!letterValue.isEmpty()) {
                        dailyLetters[trigger][day] = letterValue[0];
                    }

                    String colorValue = request->getParam(colorParam, true)->value();
                    strncpy(dailyLetterColors[trigger][day], colorValue.c_str(), COLOR_STRING_LENGTH);
                    dailyLetterColors[trigger][day][COLOR_STRING_LENGTH - 1] = '\0';
                } else {
                    success = false;
                }
            }
        }

        if (success) {
            saveConfig();
            request->send(200, "text/plain", "✅ Buchstaben & Farben gespeichert!");
        } else {
            request->send(400, "text/plain", "❌ Fehler: Nicht alle Werte empfangen!");
        }
    });

    server.on("/displayLetter", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("char")) {
            request->send(400, "text/plain", "Fehlender Parameter!");
            return;
        }

        String letter = request->getParam("char")->value();
        if (letter.length() != 1) {
            request->send(400, "text/plain", "❌ Fehler: Buchstabe muss genau ein Zeichen sein!");
            return;
        }

        uint8_t triggerIndex = 0;
        if (request->hasParam("trigger")) {
            int triggerValue = request->getParam("trigger")->value().toInt();
            if (triggerValue < 1 || triggerValue > static_cast<int>(NUM_TRIGGERS)) {
                request->send(400, "text/plain", "❌ Fehler: Ungültiger Trigger!");
                return;
            }
            triggerIndex = static_cast<uint8_t>(triggerValue - 1);
        }

        displayLetter(triggerIndex, letter[0]);
        request->send(200, "text/plain", "Buchstabe " + letter + " für Trigger " + String(triggerIndex + 1) + " angezeigt!");
    });

    server.on("/triggerLetter", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (triggerActive) {
            request->send(409, "text/plain", "❌ Fehler: Bereits aktiver Buchstabe verhindert neuen Trigger!");
            return;
        }

        uint8_t triggerIndex = 0;
        char triggerChar = '1';
        if (request->hasParam("trigger")) {
            int triggerValue = request->getParam("trigger")->value().toInt();
            if (triggerValue < 1 || triggerValue > static_cast<int>(NUM_TRIGGERS)) {
                request->send(400, "text/plain", "❌ Fehler: Ungültiger Trigger!");
                return;
            }
            triggerIndex = static_cast<uint8_t>(triggerValue - 1);
            triggerChar = static_cast<char>('0' + triggerValue);
        }

        int today = getRTCWeekday();
        if (today < 0 || today >= static_cast<int>(NUM_DAYS)) {
            request->send(500, "text/plain", "❌ Fehler: Ungültiger Wochentag vom RTC-Modul!");
            return;
        }

        char todayLetter = dailyLetters[triggerIndex][today];
        if (todayLetter != '*' && letterData.find(todayLetter) == letterData.end()) {
            request->send(500, "text/plain", "❌ Fehler: Kein Muster für den heutigen Buchstaben vorhanden!");
            return;
        }

        request->send(200, "text/plain", "✅ Buchstaben-Trigger für Trigger " + String(triggerIndex + 1) + " gestartet!");
        handleTrigger(triggerChar, false);
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
