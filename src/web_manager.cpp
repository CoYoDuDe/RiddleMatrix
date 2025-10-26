#include "web_manager.h"
#include "wifi_manager.h"
#include <AsyncJson.h>
#include <algorithm>
#include <math.h>

namespace {

constexpr size_t MAX_JSON_BODY_SIZE = 4096;
constexpr size_t UPDATE_JSON_CAPACITY = 4096;
constexpr size_t MIN_SSID_LENGTH = 2;
constexpr size_t MIN_HOSTNAME_LENGTH = 2;

struct UpdateAllLettersContext {
    String body;
    bool overflow;

    UpdateAllLettersContext() : body(), overflow(false) {}
};

bool looksLikeJsonContentType(String contentType) {
    contentType.trim();
    contentType.toLowerCase();
    return contentType.startsWith(F("application/json"));
}

String extractContentType(AsyncWebServerRequest *request) {
    if (request == nullptr) {
        return String();
    }

    if (request->hasHeader(F("Content-Type"))) {
        return request->header(F("Content-Type"));
    }
    if (request->hasHeader(F("content-type"))) {
        return request->header(F("content-type"));
    }
    return String();
}

bool isJsonRequest(AsyncWebServerRequest *request) {
    if (request == nullptr) {
        return false;
    }

    const String directContentType = extractContentType(request);
    if (!directContentType.isEmpty() && looksLikeJsonContentType(directContentType)) {
        return true;
    }

#if defined(ESP8266) || defined(ESP32)
    const String derivedContentType = request->contentType();
    if (!derivedContentType.isEmpty() && looksLikeJsonContentType(derivedContentType)) {
        return true;
    }
#endif
    return false;
}

bool isSupportedLetter(char letter) {
    const size_t optionCount = sizeof(availableLetters) / sizeof(availableLetters[0]);
    for (size_t idx = 0; idx < optionCount; ++idx) {
        if (availableLetters[idx] == letter) {
            return true;
        }
    }
    return false;
}

bool isValidHexColorString(const String &value) {
    if (value.length() != 7 || value.charAt(0) != '#') {
        return false;
    }

    for (size_t idx = 1; idx < value.length(); ++idx) {
        const char c = value.charAt(idx);
        const bool isDigit = (c >= '0' && c <= '9');
        const bool isUpper = (c >= 'A' && c <= 'F');
        const bool isLower = (c >= 'a' && c <= 'f');
        if (!(isDigit || isUpper || isLower)) {
            return false;
        }
    }

    return true;
}

bool parseDelayStringValue(String value, unsigned long &parsed) {
    value.trim();
    if (value.isEmpty()) {
        return false;
    }

    for (size_t idx = 0; idx < value.length(); ++idx) {
        const char character = value.charAt(idx);
        if (character < '0' || character > '9') {
            return false;
        }
    }

    parsed = static_cast<unsigned long>(value.toInt());
    return parsed <= 999UL;
}

bool parseNumericDelay(double numeric, unsigned long &parsed) {
    if (isnan(numeric) || isinf(numeric)) {
        return false;
    }
    if (numeric < 0.0 || numeric > 999.0) {
        return false;
    }

    const double rounded = floor(numeric + 0.5);
    if (fabs(numeric - rounded) > 0.0001) {
        return false;
    }

    parsed = static_cast<unsigned long>(rounded);
    return parsed <= 999UL;
}

bool parseDelayJsonVariant(const JsonVariantConst &variant, unsigned long &parsed) {
    if (variant.isNull()) {
        return false;
    }

    if (variant.is<unsigned long>() || variant.is<unsigned int>() || variant.is<int>() || variant.is<long>()) {
        const long candidate = variant.as<long>();
        if (candidate < 0 || candidate > 999) {
            return false;
        }
        parsed = static_cast<unsigned long>(candidate);
        return true;
    }

    if (variant.is<double>() || variant.is<float>()) {
        const double numeric = variant.as<double>();
        return parseNumericDelay(numeric, parsed);
    }

    if (variant.is<const char *>()) {
        String value = variant.as<const char *>();
        return parseDelayStringValue(value, parsed);
    }

    return false;
}

bool parseSignedLongInRange(const String &value, long minValue, long maxValue, long &parsed) {
    String sanitized = value;
    sanitized.trim();

    if (sanitized.isEmpty()) {
        return false;
    }

    char *endPtr = nullptr;
    const char *raw = sanitized.c_str();
    long candidate = strtol(raw, &endPtr, 10);

    if (endPtr == raw || (endPtr != nullptr && *endPtr != '\0')) {
        return false;
    }

    if (candidate < minValue || candidate > maxValue) {
        return false;
    }

    parsed = candidate;
    return true;
}

bool parseUnsignedLongInRange(const String &value, unsigned long minValue, unsigned long maxValue, unsigned long &parsed) {
    String sanitized = value;
    sanitized.trim();

    if (sanitized.isEmpty()) {
        return false;
    }

    char *endPtr = nullptr;
    const char *raw = sanitized.c_str();
    unsigned long candidate = strtoul(raw, &endPtr, 10);

    if (endPtr == raw || (endPtr != nullptr && *endPtr != '\0')) {
        return false;
    }

    if (candidate < minValue || candidate > maxValue) {
        return false;
    }

    parsed = candidate;
    return true;
}

void sendJsonStatus(AsyncWebServerRequest *request, uint16_t statusCode, const char *status, const String &message) {
    StaticJsonDocument<256> responseDoc;
    responseDoc["status"] = status;
    if (!message.isEmpty()) {
        responseDoc["message"] = message;
    }

    String responseBody;
    serializeJson(responseDoc, responseBody);
    request->send(statusCode, F("application/json"), responseBody);
}

String escapeHtml(const String &input) {
    String escaped;
    escaped.reserve(input.length());

    for (size_t idx = 0; idx < input.length(); ++idx) {
        const char character = input.charAt(idx);
        switch (character) {
            case '&':
                escaped += F("&amp;");
                break;
            case '<':
                escaped += F("&lt;");
                break;
            case '>':
                escaped += F("&gt;");
                break;
            case '"':
                escaped += F("&quot;");
                break;
            case '\'':
                escaped += F("&#39;");
                break;
            default:
                escaped += character;
                break;
        }
    }

    return escaped;
}

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

static_assert(NUM_DAYS == 7, "Erwartete sieben Wochentage für die JSON-Abbildung");

constexpr const char *const DAY_KEYS[NUM_DAYS] = {
    "so", "mo", "di", "mi", "do", "fr", "sa",
};

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
            .then(response => response.text().then(message => ({ ok: response.ok, message })))
            .then(result => {
                const text = result.message && result.message.trim() !== ''
                    ? result.message
                    : (result.ok ? '✅ NTP Synchronisierung erfolgreich!' : '❌ Fehler bei der NTP Synchronisierung.');
                if (!result.ok) {
                    console.warn('❌ Serverfehler:', text);
                } else {
                    console.log('ℹ️ Serverantwort:', text);
                }
                alert(text);
            })
            .catch(error => {
                console.error('❌ Fehler:', error);
                alert('❌ Fehler: ' + error);
            });
    }

    // 🔔 Buchstaben-Trigger über Webinterface
    function triggerLetter(triggerIndex) {
        let query = '';
        if (typeof triggerIndex === 'number' && triggerIndex >= 0) {
            query = '?trigger=' + encodeURIComponent(triggerIndex + 1);
        }

        fetch('/triggerLetter' + query)
            .then(response => response.text().then(message => ({ ok: response.ok, message })))
            .then(result => {
                const text = result.message && result.message.trim() !== '' ? result.message : (result.ok ? '✅ Trigger erfolgreich!' : '❌ Unbekannter Fehler beim Trigger!');
                if (!result.ok) {
                    console.warn('❌ Serverfehler:', text);
                } else {
                    console.log('ℹ️ Serverantwort:', text);
                }
                alert(text);
            })
            .catch(error => {
                console.error('❌ Fehler:', error);
                alert('❌ Fehler: ' + error);
            });
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
            .then(response => response.text().then(message => ({ ok: response.ok, message })))
            .then(result => {
                const text = result.message && result.message.trim() !== '' ? result.message : (result.ok ? '✅ Buchstabe angezeigt!' : '❌ Anzeige fehlgeschlagen!');
                if (!result.ok) {
                    console.warn('❌ Serverfehler:', text);
                } else {
                    console.log('ℹ️ Serverantwort:', text);
                }
                alert(text);
            })
            .catch(error => {
                console.error('❌ Fehler:', error);
                alert('❌ Fehler: ' + error);
            });
    }

    // 💾 WiFi-Daten speichern
    function saveWiFi() {
        const formElement = document.getElementById('wifiForm');
        let form = new FormData(formElement);

        // Checkbox explizit berücksichtigen: Nur wenn "Passwort löschen" gesetzt ist,
        // wird das Flag gesendet, ansonsten entfernen wir den Parameter.
        const removeCheckbox = document.getElementById('password_remove');
        if (removeCheckbox) {
            if (removeCheckbox.checked) {
                form.set('password_remove', 'on');
            } else {
                form.delete('password_remove');
            }
        }

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

    // 💾 Trigger-Verzögerungen speichern
    function saveTriggerDelays() {
        let form = new FormData(document.getElementById('delaysForm'));
        fetch('/updateTriggerDelays', { method: 'POST', body: form })
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
    // ℹ️ Hinweis: Der Helper refreshWiFiIdleTimer(...) aus wifi_manager.cpp muss
    //             zu Beginn jeder neuen Route mit echter Nutzerinteraktion
    //             aufgerufen werden, damit der WLAN-Timeout zuverlässig
    //             zurückgesetzt wird.
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        refreshWiFiIdleTimer(F("GET /"));
        String html = "<h1>Märchen Einstellungen</h1>";

        // **WiFi-Einstellungen**
        html += "<h2>WiFi Konfiguration</h2>";
        html += "<form id='wifiForm'>";
        html += "<p>SSID und Hostname sind Pflichtfelder (mindestens 2 Zeichen), das Passwort ist optional.</p>";
        html += "SSID: <input type='text' name='ssid' value='" + escapeHtml(String(wifi_ssid)) + "'><br>";
        html += "Passwort: <input type='password' name='password' placeholder='Leer lassen, um es zu behalten'><br>";
        html += "<label><input type='checkbox' id='password_remove' name='password_remove' value='on'> Passwort löschen</label><br>";
        html += "<p style='margin-top:4px;'>Leer gelassenes Passwort ohne Haken lässt das bisherige Passwort unverändert.</p>";
        html += "Hostname: <input type='text' name='hostname' value='" + escapeHtml(String(hostname)) + "'><br>";
        html += "<button type='button' onclick='saveWiFi()'>Speichern</button>";
        html += "</form>";

        // **Anzeige-Einstellungen**
        html += "<h2>Anzeige-Einstellungen</h2>";
        html += "<form id='displayForm'>";
        html += "Helligkeit (1–255): <input type='number' name='brightness' min='1' max='255' value='" + escapeHtml(String(display_brightness)) + "'><br>";
        html += "Buchstaben-Anzeigezeit (Sekunden, 1–60): <input type='number' name='letter_time' min='1' max='60' value='" + escapeHtml(String(letter_display_time)) + "'><br>";
        html += "Automodus-Intervall (Sekunden, 30–600): <input type='number' name='auto_interval' min='30' max='600' value='" + escapeHtml(String(letter_auto_display_interval)) + "'><br>";
        html += "<label><input type='checkbox' id='auto_mode' name='auto_mode' " + String(autoDisplayMode ? "checked='checked'" : "") + "> Automodus aktivieren</label>";
        html += "<p style='margin-top:4px;'>Zulässige Werte: Helligkeit 1–255, Anzeigezeit 1–60&nbsp;s, Automodus-Intervall 30–600&nbsp;s.</p>";
        html += "<br><button type='button' onclick='saveDisplaySettings()'>Speichern</button>";
        html += "</form>";

        html += "<h2>Trigger-Verzögerungen pro Wochentag</h2>";
        html += "<form id='delaysForm'>";
        html += "<table border='1' style='width:100%; text-align:center;'>";
        html += "<tr><th>Wochentag</th>";
        for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
            html += "<th>Trigger " + String(trigger + 1) + " (Sekunden)</th>";
        }
        html += "</tr>";

        for (size_t day = 0; day < NUM_DAYS; ++day) {
            html += "<tr>";
            html += "<td>" + escapeHtml(String(daysOfTheWeek[day])) + "</td>";

            for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
                String fieldName = "delay_" + String(trigger) + "_" + String(day);
                html += "<td><input type='number' min='0' max='999' step='1' name='" + fieldName + "' value='" + escapeHtml(String(letter_trigger_delays[trigger][day])) + "'></td>";
            }

            html += "</tr>";
        }

        html += "</table>";
        html += "<br><button type='button' onclick='saveTriggerDelays()'>Verzögerungen speichern</button>";
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
            html += "<td>" + escapeHtml(String(daysOfTheWeek[day])) + "</td>";

            for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
                String selectId = "letter_" + String(trigger) + "_" + String(day);
                String colorId = "color_" + String(trigger) + "_" + String(day);
                html += "<td>";
                html += "<select id='" + selectId + "' name='" + selectId + "'>";
                for (size_t idx = 0; idx < sizeof(availableLetters); ++idx) {
                    char optionChar = availableLetters[idx];
                    const String optionValue = escapeHtml(String(optionChar));
                    const String optionLabel = escapeHtml(getLetterOptionLabel(optionChar));
                    html += "<option value='" + optionValue + "' ";
                    html += (dailyLetters[trigger][day] == optionChar) ? "selected" : "";
                    html += ">" + optionLabel + "</option>";
                }
                html += "</select>";
                html += "<br><input type='color' id='" + colorId + "' name='" + colorId + "' value='" + escapeHtml(String(dailyLetterColors[trigger][day])) + "'>";
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
        refreshWiFiIdleTimer(F("GET /script.js"));
        request->send_P(200, "text/javascript", scriptJS);
    });

    server.on("/updateWiFi", HTTP_POST, [](AsyncWebServerRequest *request) {
        refreshWiFiIdleTimer(F("POST /updateWiFi"));
        if (request->hasParam("ssid", true) && request->hasParam("hostname", true)) {
            const String ssidParam = request->getParam("ssid", true)->value();
            const String hostnameParam = request->getParam("hostname", true)->value();
            const bool hasPasswordField = request->hasParam("password", true);
            const String passwordParam = hasPasswordField ? request->getParam("password", true)->value() : String();
            bool wantsPasswordRemoval = false;
            if (request->hasParam("password_remove", true)) {
                String removalValue = request->getParam("password_remove", true)->value();
                removalValue.trim();
                removalValue.toLowerCase();
                wantsPasswordRemoval = !removalValue.isEmpty() && removalValue != F("false") && removalValue != F("0");
            }

            auto containsForbiddenChars = [](const String &value) {
                for (size_t idx = 0; idx < value.length(); ++idx) {
                    const unsigned char character = static_cast<unsigned char>(value.charAt(idx));
                    if (character < 32 || character == 127) {
                        return true;
                    }
                }
                return false;
            };

            auto normalizeInput = [](String value) {
                value.replace('\r', ' ');
                value.replace('\n', ' ');
                value.replace('\t', ' ');
                value.trim();
                return value;
            };

            String validationError;
            if (containsForbiddenChars(ssidParam)) {
                validationError += "SSID enthält ungültige Zeichen. ";
            }
            if (containsForbiddenChars(hostnameParam)) {
                validationError += "Hostname enthält ungültige Zeichen. ";
            }
            if (hasPasswordField && !passwordParam.isEmpty() && containsForbiddenChars(passwordParam)) {
                validationError += "Passwort enthält ungültige Zeichen. ";
            }

            const String sanitizedSsid = normalizeInput(ssidParam);
            const String sanitizedHostname = normalizeInput(hostnameParam);
            const String sanitizedPassword = hasPasswordField ? normalizeInput(passwordParam) : String();
            const bool applyPassword = hasPasswordField && !sanitizedPassword.isEmpty();

            // Ein neu gesetztes Passwort hat Priorität gegenüber einem Löschwunsch.
            if (applyPassword) {
                wantsPasswordRemoval = false;
            }

            if (sanitizedSsid.length() < MIN_SSID_LENGTH) {
                validationError += "SSID muss mindestens " + String(MIN_SSID_LENGTH) + " Zeichen enthalten. ";
            }
            if (sanitizedHostname.length() < MIN_HOSTNAME_LENGTH) {
                validationError += "Hostname muss mindestens " + String(MIN_HOSTNAME_LENGTH) + " Zeichen enthalten. ";
            }

            if (!validationError.isEmpty()) {
                request->send(400, "text/plain", "❌ Fehler: " + validationError);
                return;
            }

            auto copyWithTermination = [](const String &input, char *destination, size_t destinationSize) {
                strncpy(destination, input.c_str(), destinationSize);
                destination[destinationSize - 1] = '\0';
                return input.length() >= destinationSize;
            };

            const bool ssidTruncated = copyWithTermination(sanitizedSsid, wifi_ssid, sizeof(wifi_ssid));
            const bool hostnameTruncated = copyWithTermination(sanitizedHostname, hostname, sizeof(hostname));
            bool passwordTruncated = false;
            bool passwordCleared = false;
            bool passwordUnchanged = false;

            if (applyPassword) {
                passwordTruncated = copyWithTermination(sanitizedPassword, wifi_password, sizeof(wifi_password));
            } else if (wantsPasswordRemoval) {
                for (size_t idx = 0; idx < sizeof(wifi_password); ++idx) {
                    wifi_password[idx] = '\0';
                }
                Serial.println(F("[WebManager] WLAN-Passwort zurückgesetzt."));
                passwordCleared = true;
            } else {
                passwordUnchanged = true;
            }

            saveConfig();

            String response = "✅ WiFi-Einstellungen gespeichert!";
            if (ssidTruncated) {
                response += " Hinweis: SSID wurde auf " + String(sizeof(wifi_ssid) - 1) + " Zeichen gekürzt.";
            }
            if (hostnameTruncated) {
                response += " Hinweis: Hostname wurde auf " + String(sizeof(hostname) - 1) + " Zeichen gekürzt.";
            }
            if (applyPassword && passwordTruncated) {
                response += " Hinweis: Passwort wurde auf " + String(sizeof(wifi_password) - 1) + " Zeichen gekürzt.";
            }
            if (passwordCleared) {
                response += " Hinweis: Passwort wurde gelöscht.";
            }
            if (passwordUnchanged) {
                response += " Hinweis: Passwort blieb unverändert.";
            }

            request->send(200, "text/plain", response);
        } else {
            request->send(400, "text/plain", "❌ Fehler: Fehlende Parameter!");
        }
    });

    server.on("/updateDisplaySettings", HTTP_POST, [](AsyncWebServerRequest *request) {
        refreshWiFiIdleTimer(F("POST /updateDisplaySettings"));
        if (!(request->hasParam("brightness", true) &&
              request->hasParam("letter_time", true) &&
              request->hasParam("auto_interval", true))) {
            request->send(400, "text/plain", "❌ Fehler: Alle Parameter (brightness, letter_time, auto_interval) sind erforderlich.");
            return;
        }

        AsyncWebParameter *brightnessParam = request->getParam("brightness", true);
        AsyncWebParameter *letterTimeParam = request->getParam("letter_time", true);
        AsyncWebParameter *autoIntervalParam = request->getParam("auto_interval", true);

        long brightnessCandidate = 0;
        if (!parseSignedLongInRange(brightnessParam->value(), 1L, 255L, brightnessCandidate)) {
            request->send(400, "text/plain", "❌ Fehler: Helligkeit muss eine Ganzzahl zwischen 1 und 255 sein.");
            return;
        }

        unsigned long letterTimeCandidate = 0;
        if (!parseUnsignedLongInRange(letterTimeParam->value(), 1UL, 60UL, letterTimeCandidate)) {
            request->send(400, "text/plain", "❌ Fehler: Die Anzeigezeit muss eine Ganzzahl zwischen 1 und 60 Sekunden sein.");
            return;
        }

        unsigned long autoIntervalCandidate = 0;
        if (!parseUnsignedLongInRange(autoIntervalParam->value(), 30UL, 600UL, autoIntervalCandidate)) {
            request->send(400, "text/plain", "❌ Fehler: Das Automodus-Intervall muss zwischen 30 und 600 Sekunden liegen.");
            return;
        }

        bool autoModeCandidate = false;
        if (request->hasParam("auto_mode", true)) {
            String autoModeValue = request->getParam("auto_mode", true)->value();
            autoModeValue.trim();
            autoModeValue.toLowerCase();

            if (autoModeValue == "on" || autoModeValue == "1" || autoModeValue == "true") {
                autoModeCandidate = true;
            } else if (autoModeValue.isEmpty() || autoModeValue == "off" || autoModeValue == "0" || autoModeValue == "false") {
                autoModeCandidate = false;
            } else {
                request->send(400, "text/plain", "❌ Fehler: auto_mode akzeptiert nur on/off, true/false oder 1/0.");
                return;
            }
        }

        display_brightness = static_cast<int>(brightnessCandidate);
        display.setBrightness(display_brightness);
        if (!triggerActive && wifiSymbolVisible) {
            display.display();
        }
        letter_display_time = letterTimeCandidate;
        letter_auto_display_interval = autoIntervalCandidate;
        autoDisplayMode = autoModeCandidate;

        saveConfig();
        request->send(200, "text/plain", "✅ Anzeigeeinstellungen gespeichert!");
    });

    server.on("/updateTriggerDelays", HTTP_POST, [](AsyncWebServerRequest *request) {
        refreshWiFiIdleTimer(F("POST /updateTriggerDelays"));

        unsigned long parsedDelays[NUM_TRIGGERS][NUM_DAYS];
        for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
            for (size_t day = 0; day < NUM_DAYS; ++day) {
                parsedDelays[trigger][day] = letter_trigger_delays[trigger][day];
            }
        }

        bool success = true;

        for (size_t trigger = 0; trigger < NUM_TRIGGERS && success; ++trigger) {
            for (size_t day = 0; day < NUM_DAYS; ++day) {
                String fieldName = "delay_" + String(trigger) + "_" + String(day);
                if (!request->hasParam(fieldName, true)) {
                    success = false;
                    break;
                }

                String value = request->getParam(fieldName, true)->value();
                if (value.isEmpty()) {
                    success = false;
                    break;
                }

                bool digitsOnly = true;
                for (size_t idx = 0; idx < value.length(); ++idx) {
                    char c = value.charAt(idx);
                    if (c < '0' || c > '9') {
                        digitsOnly = false;
                        break;
                    }
                }

                if (!digitsOnly) {
                    success = false;
                    break;
                }

                unsigned long parsed = static_cast<unsigned long>(value.toInt());
                if (parsed > 999UL) {
                    success = false;
                    break;
                }

                parsedDelays[trigger][day] = parsed;
            }
        }

        if (success) {
            for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
                for (size_t day = 0; day < NUM_DAYS; ++day) {
                    letter_trigger_delays[trigger][day] = parsedDelays[trigger][day];
                }
            }

            saveConfig();
            request->send(200, "text/plain", "✅ Verzögerungsmatrix gespeichert!");
        } else {
            request->send(400, "text/plain", "❌ Fehler: Ungültige oder fehlende Verzögerungswerte!");
        }
    });

    server.on("/api/trigger-delays", HTTP_GET, [](AsyncWebServerRequest *request) {
        refreshWiFiIdleTimer(F("GET /api/trigger-delays"));
        AsyncJsonResponse *response = new AsyncJsonResponse();
        JsonVariant root = response->getRoot();
        JsonObject delays = root.createNestedObject("delays");

        for (size_t day = 0; day < NUM_DAYS; ++day) {
            JsonArray dayArray = delays.createNestedArray(DAY_KEYS[day]);
            for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
                dayArray.add(static_cast<unsigned long>(letter_trigger_delays[trigger][day]));
            }
        }

        response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
        response->setLength();
        request->send(response);
    });

    server.on(
        "/updateAllLetters",
        HTTP_POST,
        [](AsyncWebServerRequest *request) {
            UpdateAllLettersContext *context = static_cast<UpdateAllLettersContext *>(request->_tempObject);
            auto cleanup = [&]() {
                if (context != nullptr) {
                    delete context;
                    request->_tempObject = nullptr;
                    context = nullptr;
                }
            };

            refreshWiFiIdleTimer(F("POST /updateAllLetters"));

            if (isJsonRequest(request)) {
                if (context == nullptr) {
                    Serial.println(F("❌ JSON-Update fehlgeschlagen: Keine Nutzlast empfangen."));
                    sendJsonStatus(request, 400, "error", F("JSON-Nutzlast fehlt oder konnte nicht gelesen werden."));
                    cleanup();
                    return;
                }

                if (context->overflow) {
                    Serial.println(F("❌ JSON-Update fehlgeschlagen: Nutzlast überschreitet Limit."));
                    String overflowMessage = F("JSON-Nutzlast überschreitet die zulässige Größe von ");
                    overflowMessage += static_cast<unsigned long>(MAX_JSON_BODY_SIZE);
                    overflowMessage += F(" Bytes.");
                    sendJsonStatus(request, 413, "error", overflowMessage);
                    cleanup();
                    return;
                }

                if (context->body.isEmpty()) {
                    Serial.println(F("❌ JSON-Update fehlgeschlagen: Leerer Request-Body."));
                    sendJsonStatus(request, 400, "error", F("JSON-Nutzlast fehlt oder ist leer."));
                    cleanup();
                    return;
                }

                DynamicJsonDocument doc(UPDATE_JSON_CAPACITY);
                DeserializationError jsonError = deserializeJson(doc, context->body);
                if (jsonError) {
                    String errorMessage = F("JSON konnte nicht gelesen werden: ");
                    errorMessage += jsonError.c_str();
                    Serial.println(String(F("❌ JSON-Parsing fehlgeschlagen: ")) + jsonError.c_str());
                    sendJsonStatus(request, 400, "error", errorMessage);
                    cleanup();
                    return;
                }

                JsonObjectConst payload = doc.as<JsonObjectConst>();
                if (payload.isNull()) {
                    Serial.println(F("❌ JSON-Update fehlgeschlagen: Payload fehlt."));
                    sendJsonStatus(request, 400, "error", F("JSON-Payload fehlt."));
                    cleanup();
                    return;
                }

                char parsedLetters[NUM_TRIGGERS][NUM_DAYS];
                char parsedColors[NUM_TRIGGERS][NUM_DAYS][COLOR_STRING_LENGTH];
                unsigned long parsedDelays[NUM_TRIGGERS][NUM_DAYS];

                bool validationFailed = false;
                String validationMessage;

                JsonObjectConst lettersObject = payload["letters"].as<JsonObjectConst>();
                if (lettersObject.isNull()) {
                    validationFailed = true;
                    validationMessage = F("JSON-Feld \"letters\" fehlt oder ist ungültig.");
                } else {
                    for (size_t day = 0; day < NUM_DAYS && !validationFailed; ++day) {
                        JsonArrayConst dayLetters = lettersObject[DAY_KEYS[day]].as<JsonArrayConst>();
                        if (dayLetters.isNull() || dayLetters.size() != NUM_TRIGGERS) {
                            validationFailed = true;
                            validationMessage = F("Ungültige Buchstabenliste für Tag ");
                            validationMessage += DAY_KEYS[day];
                            break;
                        }

                        for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
                            JsonVariantConst letterVariant = dayLetters[trigger];
                            const char *letterRaw = letterVariant.as<const char *>();
                            if (letterRaw == nullptr) {
                                validationFailed = true;
                                validationMessage = F("Buchstabe fehlt für Trigger ");
                                validationMessage += String(trigger + 1);
                                validationMessage += F(" am Tag ");
                                validationMessage += DAY_KEYS[day];
                                break;
                            }

                            String letterValue = letterRaw;
                            letterValue.trim();
                            if (letterValue.length() != 1) {
                                validationFailed = true;
                                validationMessage = F("Buchstabe muss genau ein Zeichen besitzen (Tag ");
                                validationMessage += DAY_KEYS[day];
                                validationMessage += F(", Trigger ");
                                validationMessage += String(trigger + 1);
                                validationMessage += F(").");
                                break;
                            }

                            const char letterChar = letterValue.charAt(0);
                            if (!isSupportedLetter(letterChar)) {
                                validationFailed = true;
                                validationMessage = F("Ungültiger Buchstabe für Trigger ");
                                validationMessage += String(trigger + 1);
                                validationMessage += F(" am Tag ");
                                validationMessage += DAY_KEYS[day];
                                break;
                            }

                            parsedLetters[trigger][day] = letterChar;
                        }
                    }
                }

                JsonObjectConst colorsObject = payload["colors"].as<JsonObjectConst>();
                if (!validationFailed) {
                    if (colorsObject.isNull()) {
                        validationFailed = true;
                        validationMessage = F("JSON-Feld \"colors\" fehlt oder ist ungültig.");
                    } else {
                        for (size_t day = 0; day < NUM_DAYS && !validationFailed; ++day) {
                            JsonArrayConst dayColors = colorsObject[DAY_KEYS[day]].as<JsonArrayConst>();
                            if (dayColors.isNull() || dayColors.size() != NUM_TRIGGERS) {
                                validationFailed = true;
                                validationMessage = F("Ungültige Farbliste für Tag ");
                                validationMessage += DAY_KEYS[day];
                                break;
                            }

                            for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
                                JsonVariantConst colorVariant = dayColors[trigger];
                                const char *colorRaw = colorVariant.as<const char *>();
                                if (colorRaw == nullptr) {
                                    validationFailed = true;
                                    validationMessage = F("Farbe fehlt für Trigger ");
                                    validationMessage += String(trigger + 1);
                                    validationMessage += F(" am Tag ");
                                    validationMessage += DAY_KEYS[day];
                                    break;
                                }

                                String colorValue = colorRaw;
                                colorValue.trim();
                                if (!isValidHexColorString(colorValue)) {
                                    validationFailed = true;
                                    validationMessage = F("Ungültiger Farbwert für Trigger ");
                                    validationMessage += String(trigger + 1);
                                    validationMessage += F(" am Tag ");
                                    validationMessage += DAY_KEYS[day];
                                    break;
                                }

                                colorValue.toUpperCase();
                                strncpy(parsedColors[trigger][day], colorValue.c_str(), COLOR_STRING_LENGTH);
                                parsedColors[trigger][day][COLOR_STRING_LENGTH - 1] = '\0';
                            }
                        }
                    }
                }

                JsonObjectConst delaysObject = payload["delays"].as<JsonObjectConst>();
                if (!validationFailed) {
                    if (delaysObject.isNull()) {
                        validationFailed = true;
                        validationMessage = F("JSON-Feld \"delays\" fehlt oder ist ungültig.");
                    } else {
                        for (size_t day = 0; day < NUM_DAYS && !validationFailed; ++day) {
                            JsonArrayConst dayDelays = delaysObject[DAY_KEYS[day]].as<JsonArrayConst>();
                            if (dayDelays.isNull() || dayDelays.size() != NUM_TRIGGERS) {
                                validationFailed = true;
                                validationMessage = F("Ungültige Verzögerungsliste für Tag ");
                                validationMessage += DAY_KEYS[day];
                                break;
                            }

                            for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
                                unsigned long parsedDelay = 0;
                                if (!parseDelayJsonVariant(dayDelays[trigger], parsedDelay)) {
                                    validationFailed = true;
                                    validationMessage = F("Ungültige Verzögerung für Trigger ");
                                    validationMessage += String(trigger + 1);
                                    validationMessage += F(" am Tag ");
                                    validationMessage += DAY_KEYS[day];
                                    validationMessage += F(" (erlaubt: 0-999 Sekunden).");
                                    break;
                                }
                                parsedDelays[trigger][day] = parsedDelay;
                            }
                        }
                    }
                }

                if (validationFailed) {
                    Serial.print(F("❌ JSON-Validierung fehlgeschlagen: "));
                    Serial.println(validationMessage);
                    sendJsonStatus(request, 400, "error", validationMessage);
                    cleanup();
                    return;
                }

                for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
                    for (size_t day = 0; day < NUM_DAYS; ++day) {
                        dailyLetters[trigger][day] = parsedLetters[trigger][day];
                        strncpy(dailyLetterColors[trigger][day], parsedColors[trigger][day], COLOR_STRING_LENGTH);
                        dailyLetterColors[trigger][day][COLOR_STRING_LENGTH - 1] = '\0';
                        letter_trigger_delays[trigger][day] = parsedDelays[trigger][day];
                    }
                }

                saveConfig();
                refreshWiFiIdleTimer(F("POST /updateAllLetters JSON"));
                Serial.println(F("✅ JSON-Update: Buchstaben, Farben & Verzögerungen übernommen."));
                cleanup();
                sendJsonStatus(request, 200, "ok", F("Buchstaben, Farben & Verzögerungen gespeichert."));
                return;
            }

            char parsedLetters[NUM_TRIGGERS][NUM_DAYS];
            char parsedColors[NUM_TRIGGERS][NUM_DAYS][COLOR_STRING_LENGTH];
            unsigned long parsedDelays[NUM_TRIGGERS][NUM_DAYS];
            memcpy(parsedDelays, letter_trigger_delays, sizeof(parsedDelays));

            bool success = true;
            String errorMessage;

            for (size_t trigger = 0; trigger < NUM_TRIGGERS && success; ++trigger) {
                for (size_t day = 0; day < NUM_DAYS; ++day) {
                    String letterParam = "letter_" + String(trigger) + "_" + String(day);
                    String colorParam = "color_" + String(trigger) + "_" + String(day);

                    if (!request->hasParam(letterParam, true) || !request->hasParam(colorParam, true)) {
                        success = false;
                        errorMessage = F("Nicht alle Buchstaben- oder Farbfelder wurden übermittelt.");
                        break;
                    }

                    String letterValue = request->getParam(letterParam, true)->value();
                    letterValue.trim();
                    if (letterValue.length() != 1) {
                        success = false;
                        errorMessage = F("Buchstabe muss genau ein Zeichen besitzen.");
                        break;
                    }

                    const char letterChar = letterValue.charAt(0);
                    if (!isSupportedLetter(letterChar)) {
                        success = false;
                        errorMessage = F("Ungültiger Buchstabe im Formular.");
                        break;
                    }

                    parsedLetters[trigger][day] = letterChar;

                    String colorValue = request->getParam(colorParam, true)->value();
                    colorValue.trim();
                    if (!isValidHexColorString(colorValue)) {
                        success = false;
                        errorMessage = F("Ungültiger Farbwert im Formular.");
                        break;
                    }

                    colorValue.toUpperCase();
                    strncpy(parsedColors[trigger][day], colorValue.c_str(), COLOR_STRING_LENGTH);
                    parsedColors[trigger][day][COLOR_STRING_LENGTH - 1] = '\0';
                }
            }

            bool expectDelays = false;
            if (success) {
                expectDelays = request->hasParam(F("delay_0_0"), true);
                if (expectDelays) {
                    for (size_t trigger = 0; trigger < NUM_TRIGGERS && success; ++trigger) {
                        for (size_t day = 0; day < NUM_DAYS; ++day) {
                            String delayParam = "delay_" + String(trigger) + "_" + String(day);
                            if (!request->hasParam(delayParam, true)) {
                                success = false;
                                errorMessage = F("Verzögerungswerte unvollständig übermittelt.");
                                break;
                            }

                            unsigned long parsedDelay = 0;
                            if (!parseDelayStringValue(request->getParam(delayParam, true)->value(), parsedDelay)) {
                                success = false;
                                errorMessage = F("Ungültiger Verzögerungswert im Formular (erlaubt 0-999).");
                                break;
                            }

                            parsedDelays[trigger][day] = parsedDelay;
                        }
                    }
                }
            }

            if (!success) {
                Serial.print(F("❌ Formular-Update fehlgeschlagen: "));
                Serial.println(errorMessage);
                cleanup();
                request->send(400, "text/plain", "❌ Fehler: " + errorMessage);
                return;
            }

            for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
                for (size_t day = 0; day < NUM_DAYS; ++day) {
                    dailyLetters[trigger][day] = parsedLetters[trigger][day];
                    strncpy(dailyLetterColors[trigger][day], parsedColors[trigger][day], COLOR_STRING_LENGTH);
                    dailyLetterColors[trigger][day][COLOR_STRING_LENGTH - 1] = '\0';
                    letter_trigger_delays[trigger][day] = parsedDelays[trigger][day];
                }
            }

            saveConfig();
            refreshWiFiIdleTimer(F("POST /updateAllLetters Formular"));
            if (expectDelays) {
                Serial.println(F("✅ Formular-Update: Buchstaben, Farben & Verzögerungen gespeichert."));
            } else {
                Serial.println(F("✅ Formular-Update: Buchstaben & Farben gespeichert."));
            }
            cleanup();

            String confirmation = expectDelays ? String(F("✅ Buchstaben, Farben & Verzögerungen gespeichert!"))
                                               : String(F("✅ Buchstaben & Farben gespeichert!"));
            request->send(200, "text/plain", confirmation);
        },
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (!isJsonRequest(request)) {
                return;
            }

            UpdateAllLettersContext *context = static_cast<UpdateAllLettersContext *>(request->_tempObject);
            if (context == nullptr) {
                context = new UpdateAllLettersContext();
                request->_tempObject = context;
            }

            if (context->overflow) {
                return;
            }

            if (total > MAX_JSON_BODY_SIZE) {
                context->overflow = true;
                return;
            }

            if (index == 0) {
                context->body = String();
                context->body.reserve(std::min(MAX_JSON_BODY_SIZE, total) + 1);
            }

            if (context->body.length() + len > MAX_JSON_BODY_SIZE) {
                context->overflow = true;
                return;
            }

            for (size_t idx = 0; idx < len; ++idx) {
                context->body += static_cast<char>(data[idx]);
            }
        });

    server.on("/displayLetter", HTTP_GET, [](AsyncWebServerRequest *request) {
        refreshWiFiIdleTimer(F("GET /displayLetter"));
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

        bool displayed = displayLetter(triggerIndex, letter[0]);

        if (displayed) {
            alreadyCleared = false;
            refreshWiFiIdleTimer(F("GET /displayLetter success"));
            request->send(200, "text/plain", "✅ Buchstabe " + letter + " für Trigger " + String(triggerIndex + 1) + " angezeigt!");
            return;
        }

        int statusCode = 500;
        String errorMessage = F("❌ Fehler: Anzeige fehlgeschlagen.");

        switch (lastDisplayLetterError) {
            case DisplayLetterError::TriggerAlreadyActive:
                statusCode = 409;
                errorMessage = F("❌ Fehler: Bereits aktiver Buchstabe verhindert neue Anzeige!");
                break;
            case DisplayLetterError::LetterNotFound:
                statusCode = 422;
                errorMessage = F("❌ Fehler: Kein Muster für den gewünschten Buchstaben gefunden!");
                break;
            case DisplayLetterError::InvalidWeekday:
                statusCode = 503;
                errorMessage = F("❌ Fehler: Ungültiger Wochentag vom RTC-Modul, Anzeige nicht möglich!");
                break;
            case DisplayLetterError::None:
            default:
                statusCode = 500;
                errorMessage = F("❌ Fehler: Unbekannter Anzeige-Fehler!");
                break;
        }

        request->send(statusCode, "text/plain", errorMessage);
    });

    server.on("/triggerLetter", HTTP_GET, [](AsyncWebServerRequest *request) {
        refreshWiFiIdleTimer(F("GET /triggerLetter"));
        uint8_t triggerIndex = 0;
        if (request->hasParam("trigger")) {
            int triggerValue = request->getParam("trigger")->value().toInt();
            if (triggerValue < 1 || triggerValue > static_cast<int>(NUM_TRIGGERS)) {
                request->send(400, "text/plain", "❌ Fehler: Ungültiger Trigger!");
                return;
            }
            triggerIndex = static_cast<uint8_t>(triggerValue - 1);
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

        unsigned long delaySeconds = letter_trigger_delays[triggerIndex][static_cast<size_t>(today)];
        const bool displayWasActive = triggerActive;
        const bool alreadyPendingBeforeEnqueue = isTriggerPending(triggerIndex);
        if (!enqueuePendingTrigger(triggerIndex, true)) {
            if (alreadyPendingBeforeEnqueue || isTriggerPending(triggerIndex)) {
                request->send(409, "text/plain", "❌ Fehler: Für diesen Trigger ist bereits eine Ausführung geplant!");
            } else {
                request->send(503, "text/plain", "❌ Fehler: Trigger konnte nicht eingeplant werden!");
            }
            return;
        }

        String response = "✅ Buchstaben-Trigger für Trigger " + String(triggerIndex + 1) + " eingeplant!";
        if (delaySeconds == 0) {
            response += " Start erfolgt sofort.";
        } else {
            response += " Start in " + String(delaySeconds) + " Sekunden.";
        }

        if (displayWasActive) {
            response += " Hinweis: Aktuelle Anzeige läuft noch; Ausführung erfolgt anschließend.";
        }

        request->send(200, "text/plain", response);
    });

    server.on("/getTime", HTTP_GET, [](AsyncWebServerRequest *request) {
        refreshWiFiIdleTimer(F("GET /getTime"));
        String currentTime = getRTCTime();
        request->send(200, "text/plain", currentTime);
    });

    server.on("/setTime", HTTP_POST, [](AsyncWebServerRequest *request) {
        refreshWiFiIdleTimer(F("POST /setTime"));
        if (request->hasParam("date", true) && request->hasParam("time", true)) {
            String date = request->getParam("date", true)->value();
            String time = request->getParam("time", true)->value();
            if (setRTCFromWeb(date, time)) {
                request->send(200, "text/plain", "✅ Uhrzeit erfolgreich gesetzt!");
            } else {
                request->send(400, "text/plain", "❌ Fehler: Ungültige Datum- oder Zeitangaben!");
            }
        } else {
            request->send(400, "text/plain", "❌ Fehler: Datum oder Zeit fehlt!");
        }
    });

    server.on("/syncNTP", HTTP_GET, [](AsyncWebServerRequest *request) {
        refreshWiFiIdleTimer(F("GET /syncNTP"));
        if (syncTimeWithNTP()) {
            request->send(200, "text/plain", "✅ NTP Synchronisierung erfolgreich abgeschlossen!");
        } else {
            request->send(504, "text/plain", "❌ Fehler: NTP Zeit konnte nicht abgerufen werden (Zeitüberschreitung).");
        }
    });

    server.on("/memory", HTTP_GET, [](AsyncWebServerRequest *request) {
        refreshWiFiIdleTimer(F("GET /memory"));
        request->send(200, "text/plain", String(ESP.getFreeHeap()));
    });

    server.begin();
    webServerRunning = true;
    Serial.println(F("✅ Webserver gestartet und Listener aktiv."));
}
