#include "web_manager.h"

void setupWebServer() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = "<h1>Märchen Einstellungen</h1>";

        html += "<h2>WiFi Konfiguration</h2>";
        html += "<form id='wifiForm'>";
        html += "SSID: <input type='text' name='ssid' value='" + String(wifi_ssid) + "'><br>";
        html += "Passwort: <input type='password' name='password'><br>";
        html += "Hostname: <input type='text' name='hostname' value='" + String(hostname) + "'><br>";
        html += "<button type='button' onclick='saveWiFi()'>Speichern</button>";
        html += "</form>";

        html += "<h2>Anzeigeeinstellungen</h2>";
        html += "<form id='displayForm'>";
        html += "Helligkeit: <input type='number' name='brightness' value='" + String(display_brightness) + "'><br>";
        html += "Anzeigezeit (s): <input type='number' name='letter_time' value='" + String(letter_display_time) + "'><br>";
        html += "Trigger-Delay 1 (s): <input type='number' name='trigger_delay_1' value='" + String(letter_trigger_delay_1) + "'><br>";
        html += "Trigger-Delay 2 (s): <input type='number' name='trigger_delay_2' value='" + String(letter_trigger_delay_2) + "'><br>";
        html += "Trigger-Delay 3 (s): <input type='number' name='trigger_delay_3' value='" + String(letter_trigger_delay_3) + "'><br>";
        html += "Auto-Intervall (s): <input type='number' name='auto_interval' value='" + String(letter_auto_display_interval) + "'><br>";
        html += "Automodus: <input type='checkbox' id='auto_mode' name='auto_mode'" + String(autoDisplayMode ? " checked" : "") + "><br>";
        html += "<button type='button' onclick='saveDisplaySettings()'>Speichern</button>";
        html += "</form>";

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

        html += "<h2>Manueller Trigger</h2>";
        html += "<button type='button' onclick='triggerLetter()'>Buchstaben anzeigen</button>";

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
            saveConfig();
            request->send(200, "text/plain", "✅ Buchstaben & Farben gespeichert!");
        } else {
            request->send(400, "text/plain", "❌ Fehler: Nicht alle Werte empfangen!");
        }
    });

    server.on("/displayLetter", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("char")) {
            String letter = request->getParam("char")->value();
            displayLetter(letter[0]);
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

    server.on("/memory", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", String(ESP.getFreeHeap()));
    });

    server.begin();
}

