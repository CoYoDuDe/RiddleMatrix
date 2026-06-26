#include "web_manager.h"
#include "wifi_manager.h"
#include <AsyncJson.h>
#include <algorithm>
#include <cctype>
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

bool isOpenWifiNetwork(int networkIndex) {
#if defined(ESP32)
    return WiFi.encryptionType(networkIndex) == WIFI_AUTH_OPEN;
#else
    return WiFi.encryptionType(networkIndex) == ENC_TYPE_NONE;
#endif
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

bool parseLetterColorModeValue(String value, uint8_t &mode) {
    value.trim();
    value.toLowerCase();

    if (value == "fixed") {
        mode = static_cast<uint8_t>(LetterColorMode::Fixed);
        return true;
    }
    if (value == "random_selected") {
        mode = static_cast<uint8_t>(LetterColorMode::RandomSelected);
        return true;
    }
    if (value == "random_all") {
        mode = static_cast<uint8_t>(LetterColorMode::RandomAll);
        return true;
    }

    return false;
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

bool parseTimeOfDayValue(const String &value, uint16_t &parsedMinutes) {
    String sanitized = value;
    sanitized.trim();

    int hour = 0;
    int minute = 0;
    if (sscanf(sanitized.c_str(), "%d:%d", &hour, &minute) != 2) {
        return false;
    }

    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        return false;
    }

    parsedMinutes = static_cast<uint16_t>((hour * 60) + minute);
    return true;
}

String formatMinutesAsTime(uint16_t minutesOfDay) {
    if (minutesOfDay > 1439U) {
        minutesOfDay = 0;
    }

    char buffer[6];
    snprintf(buffer, sizeof(buffer), "%02u:%02u", minutesOfDay / 60U, minutesOfDay % 60U);
    return String(buffer);
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
            return F("Zufall");
        case '#':
            return F("Sun");
        case '~':
            return F("WiFi");
        case '&':
            return F("Rad");
        case '?':
            return F("Riddler");
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
            return String(F("Symbol ")) + String(letter);
        default:
            return String(letter);
    }
}

int customSymbolIndexFromChar(char value) {
    if (value < '0' || value > '7') {
        return -1;
    }
    return value - '0';
}

String bitmapToHex(const uint8_t *bitmap) {
    static const char hexChars[] = "0123456789ABCDEF";
    String result;
    result.reserve(SYMBOL_BITMAP_SIZE * 2);
    for (size_t index = 0; index < SYMBOL_BITMAP_SIZE; ++index) {
        const uint8_t value = bitmap[index];
        result += hexChars[(value >> 4) & 0x0F];
        result += hexChars[value & 0x0F];
    }
    return result;
}

int hexDigitValue(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

bool parseBitmapHex(const String &hex, uint8_t *bitmap) {
    if (hex.length() != SYMBOL_BITMAP_SIZE * 2) {
        return false;
    }
    for (size_t index = 0; index < SYMBOL_BITMAP_SIZE; ++index) {
        const int high = hexDigitValue(hex[index * 2]);
        const int low = hexDigitValue(hex[index * 2 + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        bitmap[index] = static_cast<uint8_t>((high << 4) | low);
    }
    return true;
}

String getColorModeOptionLabel(uint8_t mode) {
    switch (static_cast<LetterColorMode>(mode)) {
        case LetterColorMode::RandomSelected:
            return F("Zufall (ausgewaehlt)");
        case LetterColorMode::RandomAll:
            return F("Zufall (alle)");
        case LetterColorMode::Fixed:
        default:
            return F("Einfarbig");
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

    function loadWiFiNetworks() {
        const select = document.getElementById('wifiNetworkSelect');
        if (!select) {
            return;
        }
        select.innerHTML = '<option>Suche Netzwerke...</option>';
        fetch('/scanWiFi')
            .then(response => response.json())
            .then(networks => {
                select.innerHTML = '<option value="">SSID aus Liste waehlen...</option>';
                networks.forEach(network => {
                    const option = document.createElement('option');
                    option.value = network.ssid;
                    option.textContent = network.ssid + ' (' + network.rssi + ' dBm' + (network.encrypted ? ', verschluesselt' : ', offen') + ')';
                    select.appendChild(option);
                });
            })
            .catch(error => {
                select.innerHTML = '<option value="">Scan fehlgeschlagen</option>';
                console.error('Fehler beim WLAN-Scan:', error);
            });
    }

    function applySelectedWiFiNetwork() {
        const select = document.getElementById('wifiNetworkSelect');
        const ssidInput = document.querySelector('#wifiForm input[name="ssid"]');
        if (select && ssidInput && select.value) {
            ssidInput.value = select.value;
        }
    }

    function updateWiFiModeFields() {
        const mode = document.querySelector('#wifiForm input[name="wifi_mode"]:checked')?.value || 'timed';
        const persistentFields = document.getElementById('persistentWifiFields');
        const apFields = document.getElementById('localApFields');
        const symbolField = document.getElementById('wifiSymbolField');
        const symbolCheckbox = document.getElementById('wifi_status_symbol_enabled');

        if (persistentFields) {
            persistentFields.style.display = mode === 'timed' ? 'none' : 'block';
        }
        if (apFields) {
            apFields.style.display = mode === 'ap_sta' ? 'block' : 'none';
        }
        if (symbolField) {
            symbolField.style.display = mode === 'timed' ? 'block' : 'none';
        }
        if (symbolCheckbox && mode !== 'timed') {
            symbolCheckbox.checked = false;
        }
    }

    function toggleStaticIpFields() {
        const enabled = document.getElementById('wifi_static_ip_enabled')?.checked;
        const fields = document.getElementById('staticIpFields');
        if (fields) {
            fields.style.display = enabled ? 'block' : 'none';
        }
    }

    // 🔔 Zeichen-/Symbol-Trigger über Webinterface
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

    // 👁️ Zeichen/Symbol direkt anzeigen
    function displayLetter(triggerIndex, letter) {
        if (typeof letter !== 'string' || letter.length !== 1) {
            console.warn('❌ Ungültiges Zeichen/Symbol:', letter);
            alert('❌ Bitte ein einzelnes Zeichen/Symbol auswählen.');
            return;
        }

        let url = '/displayLetter?char=' + encodeURIComponent(letter);
        if (typeof triggerIndex === 'number' && triggerIndex >= 0) {
            url += '&trigger=' + encodeURIComponent(triggerIndex + 1);
        }

        fetch(url)
            .then(response => response.text().then(message => ({ ok: response.ok, message })))
            .then(result => {
                const text = result.message && result.message.trim() !== '' ? result.message : (result.ok ? '✅ Zeichen/Symbol angezeigt!' : '❌ Anzeige fehlgeschlagen!');
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
    let symbolEditorBitmap = new Array(128).fill(0);

    function symbolBitIsSet(x, y) {
        const byteIndex = y * 4 + Math.floor(x / 8);
        return (symbolEditorBitmap[byteIndex] & (1 << (7 - (x % 8)))) !== 0;
    }

    function setSymbolBit(x, y, enabled) {
        const byteIndex = y * 4 + Math.floor(x / 8);
        const mask = 1 << (7 - (x % 8));
        if (enabled) {
            symbolEditorBitmap[byteIndex] |= mask;
        } else {
            symbolEditorBitmap[byteIndex] &= ~mask;
        }
    }

    function renderSymbolEditor() {
        const grid = document.getElementById('symbolGrid');
        if (!grid) {
            return;
        }
        grid.innerHTML = '';
        for (let y = 0; y < 32; y++) {
            for (let x = 0; x < 32; x++) {
                const cell = document.createElement('button');
                cell.type = 'button';
                cell.className = 'symbol-cell' + (symbolBitIsSet(x, y) ? ' on' : '');
                cell.addEventListener('click', () => {
                    setSymbolBit(x, y, !symbolBitIsSet(x, y));
                    renderSymbolEditor();
                });
                grid.appendChild(cell);
            }
        }
    }

    function symbolBitmapToHex() {
        return symbolEditorBitmap
            .map(value => (value & 255).toString(16).padStart(2, '0'))
            .join('')
            .toUpperCase();
    }

    function loadSymbolFromHex(hex) {
        const clean = String(hex || '').trim();
        if (!/^[0-9a-fA-F]{256}$/.test(clean)) {
            symbolEditorBitmap = new Array(128).fill(0);
        } else {
            symbolEditorBitmap = [];
            for (let index = 0; index < clean.length; index += 2) {
                symbolEditorBitmap.push(parseInt(clean.slice(index, index + 2), 16));
            }
        }
        renderSymbolEditor();
    }

    function loadCustomSymbol() {
        const symbol = document.getElementById('symbolSlot')?.value || 'A';
        fetch('/api/symbol-bitmap?char=' + encodeURIComponent(symbol))
            .then(response => response.json())
            .then(data => {
                loadSymbolFromHex(data.bitmap || '');
                const enabled = document.getElementById('symbolEnabled');
                if (enabled) {
                    enabled.checked = !!data.enabled;
                }
                const mode = document.getElementById('symbolEditorMode');
                if (mode) {
                    mode.textContent = data.builtin
                        ? 'Standard-Zeichen: beim Speichern wird der eingebaute Default ueberschrieben.'
                        : 'Zusatz-Zeichen: wird im Speicher abgelegt und kann direkt ausgewaehlt werden.';
                }
            })
            .catch(error => alert('Symbol konnte nicht geladen werden: ' + error));
    }

    function saveCustomSymbol() {
        const form = new FormData();
        form.append('char', document.getElementById('symbolSlot')?.value || 'A');
        form.append('enabled', document.getElementById('symbolEnabled')?.checked ? '1' : '0');
        form.append('bitmap', symbolBitmapToHex());
        fetch('/api/symbol-bitmap', { method: 'POST', body: form })
            .then(response => response.text().then(message => ({ ok: response.ok, message })))
            .then(result => alert(result.message || (result.ok ? 'Symbol gespeichert.' : 'Symbol konnte nicht gespeichert werden.')))
            .catch(error => alert('Symbol konnte nicht gespeichert werden: ' + error));
    }

    function resetSymbolToDefault() {
        const form = new FormData();
        form.append('char', document.getElementById('symbolSlot')?.value || 'A');
        form.append('clear', '1');
        fetch('/api/symbol-bitmap', { method: 'POST', body: form })
            .then(response => response.text().then(message => ({ ok: response.ok, message })))
            .then(result => {
                alert(result.message || (result.ok ? 'Default wiederhergestellt.' : 'Default konnte nicht wiederhergestellt werden.'));
                loadCustomSymbol();
            })
            .catch(error => alert('Default konnte nicht wiederhergestellt werden: ' + error));
    }

    function clearCustomSymbol() {
        symbolEditorBitmap = new Array(128).fill(0);
        renderSymbolEditor();
    }

    function importSymbolImage(input) {
        const file = input.files && input.files[0];
        if (!file) {
            return;
        }
        const image = new Image();
        image.onload = () => {
            const canvas = document.createElement('canvas');
            canvas.width = 32;
            canvas.height = 32;
            const ctx = canvas.getContext('2d');
            ctx.drawImage(image, 0, 0, 32, 32);
            const pixels = ctx.getImageData(0, 0, 32, 32).data;
            symbolEditorBitmap = new Array(128).fill(0);
            for (let y = 0; y < 32; y++) {
                for (let x = 0; x < 32; x++) {
                    const offset = (y * 32 + x) * 4;
                    const alpha = pixels[offset + 3];
                    const brightness = (pixels[offset] + pixels[offset + 1] + pixels[offset + 2]) / 3;
                    setSymbolBit(x, y, alpha > 32 && brightness < 220);
                }
            }
            renderSymbolEditor();
            URL.revokeObjectURL(image.src);
        };
        image.onerror = () => alert('Bild konnte nicht gelesen werden.');
        image.src = URL.createObjectURL(file);
    }

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

        const symbolCheckbox = document.getElementById('wifi_status_symbol_enabled');
        form.set('wifi_status_symbol_enabled', symbolCheckbox && symbolCheckbox.checked ? 'on' : 'off');
        const staticIpCheckbox = document.getElementById('wifi_static_ip_enabled');
        form.set('wifi_static_ip_enabled', staticIpCheckbox && staticIpCheckbox.checked ? 'on' : 'off');

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
        syncSharedTriggerFormFields();
        let form = new FormData(document.getElementById('delaysForm'));
        fetch('/updateTriggerDelays', { method: 'POST', body: form })
            .then(response => response.text())
            .then(alert)
            .catch(error => alert('❌ Fehler: ' + error));
    }

    // 💾 Alle Zeichen/Symbole & Farben speichern
    function saveAllLetters() {
        syncSharedTriggerFormFields();
        let formData = new FormData(document.getElementById('lettersForm'));
        fetch('/updateAllLetters', { method: 'POST', body: formData })
            .then(response => response.text())
            .then(alert)
            .catch(error => alert('❌ Fehler: ' + error));
    }

    function isSeparateTriggerEditingEnabled() {
        const checkbox = document.getElementById('separate_trigger_editing');
        return checkbox ? checkbox.checked : true;
    }

    function applyTriggerEditMode() {
        const separate = isSeparateTriggerEditingEnabled();
        document.querySelectorAll('.advanced-trigger-column').forEach(element => {
            element.style.display = separate ? '' : 'none';
        });
    }

    function syncSharedTriggerFormFields() {
        if (isSeparateTriggerEditingEnabled()) {
            return;
        }

        for (let day = 0; day < 7; day++) {
            const sourceLetter = document.getElementById('letter_0_' + day);
            const sourceColor = document.getElementById('color_0_' + day);
            const sourceColorMode = document.getElementById('color_mode_0_' + day);
            const sourceDelay = document.querySelector('input[name="delay_0_' + day + '"]');

            for (let trigger = 1; trigger < 3; trigger++) {
                const targetLetter = document.getElementById('letter_' + trigger + '_' + day);
                const targetColor = document.getElementById('color_' + trigger + '_' + day);
                const targetColorMode = document.getElementById('color_mode_' + trigger + '_' + day);
                const targetDelay = document.querySelector('input[name="delay_' + trigger + '_' + day + '"]');

                if (sourceLetter && targetLetter) {
                    targetLetter.value = sourceLetter.value;
                }
                if (sourceColor && targetColor) {
                    targetColor.value = sourceColor.value;
                }
                if (sourceColorMode && targetColorMode) {
                    targetColorMode.value = sourceColorMode.value;
                }
                if (sourceDelay && targetDelay) {
                    targetDelay.value = sourceDelay.value;
                }

                for (let paletteIndex = 0; paletteIndex < 8; paletteIndex++) {
                    const sourcePalette = document.querySelector('input[name="palette_0_' + day + '_' + paletteIndex + '"]');
                    const targetPalette = document.querySelector('input[name="palette_' + trigger + '_' + day + '_' + paletteIndex + '"]');
                    if (sourcePalette && targetPalette) {
                        targetPalette.checked = sourcePalette.checked;
                    }
                }
            }
        }
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

    document.querySelectorAll('#wifiForm input[name="wifi_mode"]').forEach(input => {
        input.addEventListener('change', updateWiFiModeFields);
    });
    const staticIpCheckbox = document.getElementById('wifi_static_ip_enabled');
    if (staticIpCheckbox) {
        staticIpCheckbox.addEventListener('change', toggleStaticIpFields);
    }
    updateWiFiModeFields();
    toggleStaticIpFields();

    applyTriggerEditMode();
)rawliteral";

void setupWebServer() {
    DefaultHeaders::Instance().addHeader(F("Access-Control-Allow-Origin"), F("*"));
    DefaultHeaders::Instance().addHeader(F("Access-Control-Allow-Methods"), F("GET, POST, OPTIONS"));
    DefaultHeaders::Instance().addHeader(F("Access-Control-Allow-Headers"), F("Content-Type"));
    DefaultHeaders::Instance().addHeader(F("Access-Control-Allow-Private-Network"), F("true"));
    // ℹ️ Hinweis: Der Helper refreshWiFiIdleTimer(...) aus wifi_manager.cpp muss
    //             zu Beginn jeder neuen Route mit echter Nutzerinteraktion
    //             aufgerufen werden, damit der WLAN-Timeout zuverlässig
    //             zurückgesetzt wird.
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        refreshWiFiIdleTimer(F("GET /"));
        String html = "<h1>RiddleMatrix Einstellungen</h1>";

        // **WiFi-Einstellungen**
        html += "<h2>WiFi Konfiguration</h2>";
        html += "<form id='wifiForm'>";
        html += "<p>Standard bleibt: Box verbindet sich nur beim Start zur Verwaltung und schaltet WLAN nach Inaktivitaet wieder ab.</p>";
        html += "<fieldset><legend>WLAN-Modus</legend>";
        html += "<label><input type='radio' name='wifi_mode' value='timed' ";
        html += (wifi_operation_mode == static_cast<uint8_t>(WiFiOperationMode::TimedManager) ? "checked" : "");
        html += "> Standard: Manager/Hotspot zeitweise</label><br>";
        html += "<label><input type='radio' name='wifi_mode' value='always' ";
        html += (wifi_operation_mode == static_cast<uint8_t>(WiFiOperationMode::AlwaysConnected) ? "checked" : "");
        html += "> Dauerhaft mit bestehendem WLAN verbinden</label><br>";
        html += "<label><input type='radio' name='wifi_mode' value='ap_sta' ";
        html += (wifi_operation_mode == static_cast<uint8_t>(WiFiOperationMode::StaWithLocalAp) ? "checked" : "");
        html += "> AP+STA/Mesh-Kopie: WLAN verbinden und zusaetzlichen Box-AP starten</label>";
        html += "</fieldset>";
        html += "<p>SSID und Hostname sind Pflichtfelder (mindestens 2 Zeichen), das Passwort ist optional.</p>";
        html += "Netzwerke: <select id='wifiNetworkSelect' onchange='applySelectedWiFiNetwork()'><option value=''>Noch nicht gesucht</option></select> ";
        html += "<button type='button' onclick='loadWiFiNetworks()'>WLAN suchen</button><br>";
        html += "SSID: <input type='text' name='ssid' value='" + escapeHtml(String(wifi_ssid)) + "'><br>";
        html += "Passwort: <input type='password' name='password' placeholder='Leer lassen, um es zu behalten'><br>";
        html += "<label><input type='checkbox' id='password_remove' name='password_remove' value='on'> Passwort loeschen</label><br>";
        html += "<p style='margin-top:4px;'>Leer gelassenes Passwort ohne Haken laesst das bisherige Passwort unveraendert.</p>";
        html += "Hostname: <input type='text' name='hostname' value='" + escapeHtml(String(hostname)) + "'><br>";
        html += "<div id='wifiSymbolField'><label><input type='checkbox' id='wifi_status_symbol_enabled' name='wifi_status_symbol_enabled' value='on' ";
        html += (wifi_status_symbol_enabled ? "checked" : "");
        html += "> WiFi-Symbol im Standardmodus anzeigen</label></div>";
        html += "<div id='persistentWifiFields' style='display:none; border-left:3px solid #999; padding-left:10px; margin:8px 0;'>";
        html += "<p>In dauerhaften WLAN-Modi bleibt die Box online, reconnectet automatisch und zeigt kein WiFi-Symbol auf der Matrix.</p>";
        html += "<label><input type='checkbox' id='wifi_static_ip_enabled' name='wifi_static_ip_enabled' value='on' ";
        html += (wifi_static_ip_enabled ? "checked" : "");
        html += "> Statische IP verwenden</label>";
        html += "<div id='staticIpFields' style='display:none; margin:6px 0;'>";
        html += "IP: <input type='text' name='static_ip' value='" + escapeHtml(String(wifi_static_ip)) + "'><br>";
        html += "Gateway: <input type='text' name='gateway' value='" + escapeHtml(String(wifi_gateway)) + "'><br>";
        html += "Subnetz: <input type='text' name='subnet' value='" + escapeHtml(String(wifi_subnet)) + "'><br>";
        html += "DNS: <input type='text' name='dns' value='" + escapeHtml(String(wifi_dns)) + "'><br>";
        html += "</div>";
        html += "</div>";
        html += "<div id='localApFields' style='display:none; border-left:3px solid #999; padding-left:10px; margin:8px 0;'>";
        html += "<p>AP+STA startet einen lokalen Box-AP mit denselben Zugangsdaten wie das Ziel-WLAN, sofern hier nichts anderes eingetragen wird.</p>";
        html += "Lokale AP-SSID: <input type='text' name='local_ap_ssid' value='" + escapeHtml(String(wifi_local_ap_ssid)) + "'><br>";
        html += "Lokales AP-Passwort: <input type='password' name='local_ap_password' placeholder='Leer lassen = WLAN-Passwort uebernehmen'><br>";
        html += "</div>";
        html += "<button type='button' onclick='saveWiFi()'>Speichern</button>";
        html += "</form>";
        // **Anzeige-Einstellungen**
        html += "<h2>Anzeige-Einstellungen</h2>";
        html += "<form id='displayForm'>";
        html += "Helligkeit (1–255): <input type='number' name='brightness' min='1' max='255' value='" + escapeHtml(String(display_brightness)) + "'><br>";
        html += "Zeichen-Anzeigezeit (Sekunden, 1–60): <input type='number' name='letter_time' min='1' max='60' value='" + escapeHtml(String(letter_display_time)) + "'><br>";
        html += "Automodus-Intervall (Sekunden, 30–600): <input type='number' name='auto_interval' min='30' max='600' value='" + escapeHtml(String(letter_auto_display_interval)) + "'><br>";
        html += "Zufalls-Zeichen bei *: <input type='text' name='random_symbol_pool' maxlength='39' value='" + escapeHtml(String(random_symbol_pool)) + "'><br>";
        html += "Standalone aktiv von: <input type='time' name='active_start' value='" + escapeHtml(formatMinutesAsTime(standalone_active_start_minutes)) + "'><br>";
        html += "Standalone aktiv bis: <input type='time' name='active_end' value='" + escapeHtml(formatMinutesAsTime(standalone_active_end_minutes)) + "'><br>";
        html += "<label><input type='checkbox' id='auto_mode' name='auto_mode' " + String(autoDisplayMode ? "checked='checked'" : "") + "> Automodus aktivieren</label>";
        html += "<p style='margin-top:4px;'>Zulässige Werte: Helligkeit 1–255, Anzeigezeit 1–60&nbsp;s, Automodus-Intervall 30–600&nbsp;s. Aktivzeiten im Format HH:MM; gleicher Start- und Endwert bedeutet 24-Stunden-Betrieb.</p>";
        html += "<br><button type='button' onclick='saveDisplaySettings()'>Speichern</button>";
        html += "</form>";

        html += "<h2>Trigger-Verzögerungen pro Wochentag</h2>";
        html += "<label><input type='checkbox' id='separate_trigger_editing' onchange='applyTriggerEditMode()'> Trigger 2 und 3 separat bearbeiten</label>";
        html += "<p style='margin-top:4px;'>Ohne Haken werden die Werte von Trigger 1 beim Speichern auf alle Trigger kopiert.</p>";
        html += "<form id='delaysForm'>";
        html += "<table border='1' style='width:100%; text-align:center;'>";
        html += "<tr><th>Wochentag</th>";
        for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
            String classAttribute = trigger == 0 ? "" : " class='advanced-trigger-column'";
            html += "<th" + classAttribute + ">Trigger " + String(trigger + 1) + " (Sekunden)</th>";
        }
        html += "</tr>";

        for (size_t day = 0; day < NUM_DAYS; ++day) {
            html += "<tr>";
            html += "<td>" + escapeHtml(String(daysOfTheWeek[day])) + "</td>";

            for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
                String fieldName = "delay_" + String(trigger) + "_" + String(day);
                String classAttribute = trigger == 0 ? "" : " class='advanced-trigger-column'";
                html += "<td" + classAttribute + "><input type='number' min='0' max='999' step='1' name='" + fieldName + "' value='" + escapeHtml(String(letter_trigger_delays[trigger][day])) + "'></td>";
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

        // **Zeichen-/Symbolauswahl pro Wochentag**
        html += "<h2>Zeichen/Symbole pro Wochentag &amp; Trigger</h2>";
        html += "<form id='lettersForm'>";
        html += "<table border='1' style='width:100%; text-align:center;'>";
        html += "<tr><th>Wochentag</th>";
        for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
            String classAttribute = trigger == 0 ? "" : " class='advanced-trigger-column'";
            html += "<th" + classAttribute + ">Trigger " + String(trigger + 1) + "</th>";
        }
        html += "</tr>";

        for (size_t day = 0; day < NUM_DAYS; ++day) {
            html += "<tr>";
            html += "<td>" + escapeHtml(String(daysOfTheWeek[day])) + "</td>";

            for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
                String selectId = "letter_" + String(trigger) + "_" + String(day);
                String colorId = "color_" + String(trigger) + "_" + String(day);
                String colorModeId = "color_mode_" + String(trigger) + "_" + String(day);
                String classAttribute = trigger == 0 ? "" : " class='advanced-trigger-column'";
                html += "<td" + classAttribute + ">";
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
                html += "<br><select id='" + colorModeId + "' name='" + colorModeId + "'>";
                for (uint8_t modeValue = static_cast<uint8_t>(LetterColorMode::Fixed);
                     modeValue <= static_cast<uint8_t>(LetterColorMode::RandomAll);
                     ++modeValue) {
                    html += "<option value='";
                    if (modeValue == static_cast<uint8_t>(LetterColorMode::Fixed)) {
                        html += "fixed";
                    } else if (modeValue == static_cast<uint8_t>(LetterColorMode::RandomSelected)) {
                        html += "random_selected";
                    } else {
                        html += "random_all";
                    }
                    html += "' ";
                    if (dailyLetterColorModes[trigger][day] == modeValue) {
                        html += "selected";
                    }
                    html += ">" + escapeHtml(getColorModeOptionLabel(modeValue)) + "</option>";
                }
                html += "</select>";
                html += "<details style='margin-top:4px;'><summary>Zufallspalette</summary>";
                for (size_t paletteIndex = 0; paletteIndex < RANDOM_COLOR_PALETTE_SIZE; ++paletteIndex) {
                    String paletteField = "palette_" + String(trigger) + "_" + String(day) + "_" + String(paletteIndex);
                    const bool paletteSelected =
                        (dailyLetterRandomPaletteMasks[trigger][day] & static_cast<uint16_t>(1U << paletteIndex)) != 0U;
                    html += "<label style='display:inline-block; margin:2px 6px 2px 0;'>";
                    html += "<input type='checkbox' name='" + paletteField + "' value='1' ";
                    if (paletteSelected) {
                        html += "checked";
                    }
                    html += ">";
                    html += "<span style='display:inline-block; width:10px; height:10px; border:1px solid #333; background:";
                    html += escapeHtml(String(randomColorPalette[paletteIndex]));
                    html += "; margin:0 4px;'></span>";
                    html += escapeHtml(String(randomColorPaletteLabels[paletteIndex]));
                    html += "</label>";
                }
                html += "</details>";
                html += "<br><button type='button' onclick='displayLetter(" + String(trigger) + ", document.getElementById(\"" + selectId + "\").value)'>Anzeigen</button>";
                html += "<br><button type='button' onclick='triggerLetter(" + String(trigger) + ")'>Triggern</button>";
                html += "</td>";
            }

            html += "</tr>";
        }

        html += "</table>";
        html += "<br><button type='button' onclick='saveAllLetters()'>Alle speichern</button>";
        html += "</form>";

        html += "<h2>Zeichen/Symbole bearbeiten</h2>";
        html += "<p>Alle vorhandenen Zeichen/Symbole sind bearbeitbar. A-Z sowie Sun, WIFI, Rad und Riddler werden als Overrides gespeichert. Die Zusatzzeichen 0 bis 7 sind acht weitere frei benennbare Zeichen; mehr Zusatzzeichen sind auf den kleinen Boxen wegen Speicher und Firmware-Groesse bewusst nicht vorgesehen.</p>";
        html += "<style>.symbol-editor{display:flex;gap:16px;align-items:flex-start;flex-wrap:wrap}.symbol-grid{display:grid;grid-template-columns:repeat(32,12px);gap:1px;background:#333;padding:4px;width:max-content}.symbol-cell{width:12px;height:12px;border:0;background:#f3f3f3;padding:0;cursor:pointer}.symbol-cell.on{background:#111}.symbol-actions{display:flex;flex-direction:column;gap:8px;max-width:320px}</style>";
        html += "<div class='symbol-editor'>";
        html += "<div id='symbolGrid' class='symbol-grid'></div>";
        html += "<div class='symbol-actions'>";
        html += "<label>Zeichen/Symbol: <select id='symbolSlot' onchange='loadCustomSymbol()'>";
        for (size_t idx = 0; idx < sizeof(availableLetters); ++idx) {
            char optionChar = availableLetters[idx];
            if (optionChar == '*') {
                continue;
            }
            html += "<option value='" + escapeHtml(String(optionChar)) + "'>" + escapeHtml(getLetterOptionLabel(optionChar)) + "</option>";
        }
        html += "</select></label>";
        html += "<p id='symbolEditorMode' style='margin:0;color:#555;'>Standard-Zeichen: beim Speichern wird der eingebaute Default ueberschrieben.</p>";
        html += "<label><input id='symbolEnabled' type='checkbox'> Override/Zusatz-Zeichen aktiv</label>";
        html += "<label>Bild importieren (PNG/JPG/BMP): <input type='file' accept='image/*,.bmp' onchange='importSymbolImage(this)'></label>";
        html += "<button type='button' onclick='saveCustomSymbol()'>Zeichen/Symbol speichern</button>";
        html += "<button type='button' onclick='resetSymbolToDefault()'>Eingebauten Default wiederherstellen</button>";
        html += "<button type='button' onclick='clearCustomSymbol()'>Raster leeren</button>";
        html += "<button type='button' onclick='displayLetter(0, document.getElementById(\"symbolSlot\").value)'>Zeichen/Symbol anzeigen</button>";
        html += "</div></div>";
        html += "<script>setTimeout(loadCustomSymbol, 100);</script>";
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

    server.on("/scanWiFi", HTTP_GET, [](AsyncWebServerRequest *request) {
        refreshWiFiIdleTimer(F("GET /scanWiFi"));
        const WiFiMode_t previousMode = WiFi.getMode();
        if (previousMode == WIFI_OFF) {
            WiFi.mode(WIFI_STA);
        }

        const int networkCount = WiFi.scanNetworks();
        StaticJsonDocument<1536> responseDoc;
        JsonArray networks = responseDoc.to<JsonArray>();
        for (int index = 0; index < networkCount && index < 20; ++index) {
            JsonObject network = networks.createNestedObject();
            network["ssid"] = WiFi.SSID(index);
            network["rssi"] = WiFi.RSSI(index);
            network["encrypted"] = !isOpenWifiNetwork(index);
        }
        WiFi.scanDelete();
        if (previousMode == WIFI_OFF) {
            WiFi.mode(WIFI_OFF);
        }

        String responseBody;
        serializeJson(responseDoc, responseBody);
        request->send(200, F("application/json"), responseBody);
    });

    server.on("/api/custom-symbol", HTTP_GET, [](AsyncWebServerRequest *request) {
        refreshWiFiIdleTimer(F("GET /api/custom-symbol"));
        if (!request->hasParam(F("slot"))) {
            request->send(400, F("text/plain"), F("slot fehlt"));
            return;
        }

        const String slotValue = request->getParam(F("slot"))->value();
        const int slot = slotValue.toInt();
        if (slot < 0 || slot >= static_cast<int>(CUSTOM_SYMBOL_COUNT)) {
            request->send(400, F("text/plain"), F("ungueltiger slot"));
            return;
        }

        StaticJsonDocument<384> responseDoc;
        responseDoc["slot"] = slot;
        responseDoc["enabled"] = customSymbolEnabled[slot] == 1;
        responseDoc["bitmap"] = bitmapToHex(customSymbolBitmaps[slot]);
        String responseBody;
        serializeJson(responseDoc, responseBody);
        request->send(200, F("application/json"), responseBody);
    });

    server.on("/api/symbol-bitmap", HTTP_GET, [](AsyncWebServerRequest *request) {
        refreshWiFiIdleTimer(F("GET /api/symbol-bitmap"));
        if (!request->hasParam(F("char"))) {
            request->send(400, F("text/plain"), F("char fehlt"));
            return;
        }

        const String charValue = request->getParam(F("char"))->value();
        if (charValue.length() != 1 || charValue[0] == '*' || !isSupportedLetter(charValue[0])) {
            request->send(400, F("text/plain"), F("ungueltiges Zeichen/Symbol"));
            return;
        }

        const char symbol = charValue[0];
        StaticJsonDocument<1536> responseDoc;
        responseDoc["char"] = charValue;
        responseDoc["label"] = getLetterOptionLabel(symbol);

        const int customSlot = customSymbolIndexFromChar(symbol);
        if (customSlot >= 0) {
            responseDoc["builtin"] = false;
            responseDoc["enabled"] = customSymbolEnabled[customSlot] == 1;
            responseDoc["bitmap"] = bitmapToHex(customSymbolBitmaps[customSlot]);
        } else {
            uint8_t bitmap[SYMBOL_BITMAP_SIZE] = {};
            const int builtinIndex = editableBuiltinSymbolIndexFromChar(symbol);
            const bool overrideEnabled =
                builtinIndex >= 0 && editableBuiltinSymbolEnabled[builtinIndex] == 1 &&
                getEditableBuiltinSymbolBitmap(symbol, bitmap);
            if (!overrideEnabled && !getDefaultBuiltinSymbolBitmap(symbol, bitmap)) {
                request->send(404, F("text/plain"), F("kein Bitmap-Default gefunden"));
                return;
            }
            uint8_t defaultBitmap[SYMBOL_BITMAP_SIZE] = {};
            getDefaultBuiltinSymbolBitmap(symbol, defaultBitmap);
            responseDoc["builtin"] = true;
            responseDoc["enabled"] = overrideEnabled;
            responseDoc["bitmap"] = bitmapToHex(bitmap);
            responseDoc["defaultBitmap"] = bitmapToHex(defaultBitmap);
        }

        String responseBody;
        serializeJson(responseDoc, responseBody);
        request->send(200, F("application/json"), responseBody);
    });

    server.on("/api/symbol-bitmap", HTTP_POST, [](AsyncWebServerRequest *request) {
        refreshWiFiIdleTimer(F("POST /api/symbol-bitmap"));
        if (!request->hasParam(F("char"), true)) {
            request->send(400, F("text/plain"), F("char fehlt"));
            return;
        }

        const String charValue = request->getParam(F("char"), true)->value();
        if (charValue.length() != 1 || charValue[0] == '*' || !isSupportedLetter(charValue[0])) {
            request->send(400, F("text/plain"), F("ungueltiges Zeichen/Symbol"));
            return;
        }

        const char symbol = charValue[0];
        const bool clearRequested =
            request->hasParam(F("clear"), true) && request->getParam(F("clear"), true)->value() == F("1");
        const int customSlot = customSymbolIndexFromChar(symbol);

        if (clearRequested) {
            if (customSlot >= 0) {
                memset(customSymbolBitmaps[customSlot], 0, SYMBOL_BITMAP_SIZE);
                customSymbolEnabled[customSlot] = 0;
                saveConfig();
                request->send(200, F("text/plain"), F("Zusatz-Zeichen geleert."));
                return;
            }
            if (!clearEditableBuiltinSymbol(symbol)) {
                request->send(500, F("text/plain"), F("Default konnte nicht wiederhergestellt werden."));
                return;
            }
            request->send(200, F("text/plain"), F("Eingebauter Default wiederhergestellt."));
            return;
        }

        if (!request->hasParam(F("bitmap"), true)) {
            request->send(400, F("text/plain"), F("bitmap fehlt"));
            return;
        }

        uint8_t parsedBitmap[SYMBOL_BITMAP_SIZE] = {};
        if (!parseBitmapHex(request->getParam(F("bitmap"), true)->value(), parsedBitmap)) {
            request->send(400, F("text/plain"), F("bitmap muss 256 Hex-Zeichen enthalten"));
            return;
        }

        const bool enabled =
            request->hasParam(F("enabled"), true) && request->getParam(F("enabled"), true)->value() == F("1");

        if (customSlot >= 0) {
            memcpy(customSymbolBitmaps[customSlot], parsedBitmap, SYMBOL_BITMAP_SIZE);
            customSymbolEnabled[customSlot] = enabled ? 1 : 0;
            saveConfig();
            request->send(200, F("text/plain"), F("Zusatz-Zeichen gespeichert."));
            return;
        }

        if (!saveEditableBuiltinSymbol(symbol, parsedBitmap, enabled)) {
            request->send(500, F("text/plain"), F("Zeichen/Symbol konnte nicht gespeichert werden."));
            return;
        }
        request->send(200, F("text/plain"), F("Zeichen/Symbol gespeichert."));
    });

    server.on("/api/custom-symbol", HTTP_POST, [](AsyncWebServerRequest *request) {
        refreshWiFiIdleTimer(F("POST /api/custom-symbol"));
        if (!request->hasParam(F("slot"), true) || !request->hasParam(F("bitmap"), true)) {
            request->send(400, F("text/plain"), F("slot oder bitmap fehlt"));
            return;
        }

        const int slot = request->getParam(F("slot"), true)->value().toInt();
        if (slot < 0 || slot >= static_cast<int>(CUSTOM_SYMBOL_COUNT)) {
            request->send(400, F("text/plain"), F("ungueltiger slot"));
            return;
        }

        uint8_t parsedBitmap[SYMBOL_BITMAP_SIZE] = {};
        if (!parseBitmapHex(request->getParam(F("bitmap"), true)->value(), parsedBitmap)) {
            request->send(400, F("text/plain"), F("bitmap muss 256 Hex-Zeichen enthalten"));
            return;
        }

        memcpy(customSymbolBitmaps[slot], parsedBitmap, SYMBOL_BITMAP_SIZE);
        customSymbolEnabled[slot] =
            request->hasParam(F("enabled"), true) && request->getParam(F("enabled"), true)->value() == F("1")
                ? 1
                : 0;
        saveConfig();
        request->send(200, F("text/plain"), F("Symbol gespeichert."));
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

            auto parseCheckbox = [](AsyncWebServerRequest *currentRequest, const char *name) {
                if (!currentRequest->hasParam(name, true)) {
                    return false;
                }
                String value = currentRequest->getParam(name, true)->value();
                value.trim();
                value.toLowerCase();
                return value == F("on") || value == F("1") || value == F("true") || value == F("yes");
            };

            auto parseIPv4 = [](const String &value, IPAddress &parsed) {
                return parsed.fromString(value);
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
            String wifiModeValue = request->hasParam("wifi_mode", true) ? request->getParam("wifi_mode", true)->value() : String("timed");
            wifiModeValue.trim();
            wifiModeValue.toLowerCase();
            uint8_t requestedMode = static_cast<uint8_t>(WiFiOperationMode::TimedManager);
            if (wifiModeValue == F("always")) {
                requestedMode = static_cast<uint8_t>(WiFiOperationMode::AlwaysConnected);
            } else if (wifiModeValue == F("ap_sta")) {
                requestedMode = static_cast<uint8_t>(WiFiOperationMode::StaWithLocalAp);
            } else if (wifiModeValue != F("timed")) {
                validationError += "Unbekannter WLAN-Modus. ";
            }

            const bool requestedStatusSymbol =
                requestedMode == static_cast<uint8_t>(WiFiOperationMode::TimedManager) &&
                parseCheckbox(request, "wifi_status_symbol_enabled");
            const bool requestedStaticIp = parseCheckbox(request, "wifi_static_ip_enabled");
            const String requestedStaticIpValue = normalizeInput(request->hasParam("static_ip", true) ? request->getParam("static_ip", true)->value() : String(wifi_static_ip));
            const String requestedGatewayValue = normalizeInput(request->hasParam("gateway", true) ? request->getParam("gateway", true)->value() : String(wifi_gateway));
            const String requestedSubnetValue = normalizeInput(request->hasParam("subnet", true) ? request->getParam("subnet", true)->value() : String(wifi_subnet));
            const String requestedDnsValue = normalizeInput(request->hasParam("dns", true) ? request->getParam("dns", true)->value() : String(wifi_dns));
            const String requestedLocalApSsid = normalizeInput(request->hasParam("local_ap_ssid", true) ? request->getParam("local_ap_ssid", true)->value() : String(wifi_local_ap_ssid));
            const String requestedLocalApPasswordRaw = normalizeInput(request->hasParam("local_ap_password", true) ? request->getParam("local_ap_password", true)->value() : String());
            const String requestedLocalApPassword = requestedLocalApPasswordRaw.isEmpty() ? (applyPassword ? sanitizedPassword : String(wifi_password)) : requestedLocalApPasswordRaw;

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
            if (requestedMode == static_cast<uint8_t>(WiFiOperationMode::StaWithLocalAp) &&
                !requestedLocalApSsid.isEmpty() &&
                requestedLocalApSsid.length() < MIN_SSID_LENGTH) {
                validationError += "Lokale AP-SSID muss mindestens " + String(MIN_SSID_LENGTH) + " Zeichen enthalten. ";
            }
            if (requestedStaticIp) {
                IPAddress parsedIp;
                IPAddress parsedGateway;
                IPAddress parsedSubnet;
                IPAddress parsedDns;
                if (!parseIPv4(requestedStaticIpValue, parsedIp)) {
                    validationError += "Statische IP ist ungueltig. ";
                }
                if (!parseIPv4(requestedGatewayValue, parsedGateway)) {
                    validationError += "Gateway ist ungueltig. ";
                }
                if (!parseIPv4(requestedSubnetValue, parsedSubnet)) {
                    validationError += "Subnetz ist ungueltig. ";
                }
                if (!parseIPv4(requestedDnsValue, parsedDns)) {
                    validationError += "DNS ist ungueltig. ";
                }
            }
            if (containsForbiddenChars(requestedLocalApSsid) || containsForbiddenChars(requestedLocalApPassword)) {
                validationError += "Lokale AP-Daten enthalten ungueltige Zeichen. ";
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
            bool appliedInfraDefaults = false;

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

            if (requestedMode != static_cast<uint8_t>(WiFiOperationMode::TimedManager) &&
                sanitizedSsid == String(RIDDLEMATRIX_DEFAULT_WIFI_SSID) &&
                !applyPassword) {
                copyWithTermination(String(DEFAULT_INFRA_WIFI_SSID), wifi_ssid, sizeof(wifi_ssid));
                copyWithTermination(String(DEFAULT_INFRA_WIFI_PASSWORD), wifi_password, sizeof(wifi_password));
                appliedInfraDefaults = true;
                passwordUnchanged = false;
            }

            wifi_operation_mode = requestedMode;
            wifi_status_symbol_enabled = requestedStatusSymbol;
            wifi_static_ip_enabled = requestedStaticIp;
            copyWithTermination(requestedStaticIpValue, wifi_static_ip, sizeof(wifi_static_ip));
            copyWithTermination(requestedGatewayValue, wifi_gateway, sizeof(wifi_gateway));
            copyWithTermination(requestedSubnetValue, wifi_subnet, sizeof(wifi_subnet));
            copyWithTermination(requestedDnsValue, wifi_dns, sizeof(wifi_dns));
            String localApSsidToStore = requestedLocalApSsid;
            String localApPasswordToStore = requestedLocalApPassword;
            if (requestedMode == static_cast<uint8_t>(WiFiOperationMode::StaWithLocalAp)) {
                if (localApSsidToStore.isEmpty() || localApSsidToStore == F("RiddleMatrix-Box")) {
                    localApSsidToStore = String(wifi_ssid);
                }
                if (requestedLocalApPasswordRaw.isEmpty()) {
                    localApPasswordToStore = String(wifi_password);
                }
            }
            copyWithTermination(localApSsidToStore, wifi_local_ap_ssid, sizeof(wifi_local_ap_ssid));
            copyWithTermination(localApPasswordToStore, wifi_local_ap_password, sizeof(wifi_local_ap_password));

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

            if (appliedInfraDefaults) {
                response += " Hinweis: Ziel-WLAN wurde auf " + String(DEFAULT_INFRA_WIFI_SSID) + " / " + String(DEFAULT_INFRA_WIFI_PASSWORD) + " gesetzt.";
            }

            if (requestedMode == static_cast<uint8_t>(WiFiOperationMode::TimedManager)) {
                response += " Modus: Standard/zeitweise.";
            } else if (requestedMode == static_cast<uint8_t>(WiFiOperationMode::AlwaysConnected)) {
                response += " Modus: dauerhaftes WLAN. WiFi-Symbol bleibt aus.";
            } else {
                response += " Modus: AP+STA/Mesh-Kopie. WiFi-Symbol bleibt aus.";
            }
            response += " Neustart der Box empfohlen, damit WLAN-Modus und IP-Konfiguration sauber neu starten.";

            request->send(200, "text/plain", response);
        } else {
            request->send(400, "text/plain", "❌ Fehler: Fehlende Parameter!");
        }
    });

    server.on("/updateDisplaySettings", HTTP_POST, [](AsyncWebServerRequest *request) {
        refreshWiFiIdleTimer(F("POST /updateDisplaySettings"));
        if (!(request->hasParam("brightness", true) &&
              request->hasParam("letter_time", true) &&
              request->hasParam("auto_interval", true) &&
              request->hasParam("active_start", true) &&
              request->hasParam("active_end", true))) {
            request->send(400, "text/plain", "❌ Fehler: Alle Parameter (brightness, letter_time, auto_interval, active_start, active_end) sind erforderlich.");
            return;
        }

        const AsyncWebParameter *brightnessParam = request->getParam("brightness", true);
        const AsyncWebParameter *letterTimeParam = request->getParam("letter_time", true);
        const AsyncWebParameter *autoIntervalParam = request->getParam("auto_interval", true);
        const AsyncWebParameter *activeStartParam = request->getParam("active_start", true);
        const AsyncWebParameter *activeEndParam = request->getParam("active_end", true);
        const String randomPoolParam = request->hasParam("random_symbol_pool", true)
            ? request->getParam("random_symbol_pool", true)->value()
            : String(random_symbol_pool);

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

        uint16_t activeStartCandidate = 0;
        if (!parseTimeOfDayValue(activeStartParam->value(), activeStartCandidate)) {
            request->send(400, "text/plain", "❌ Fehler: active_start muss im Format HH:MM zwischen 00:00 und 23:59 liegen.");
            return;
        }

        uint16_t activeEndCandidate = 0;
        if (!parseTimeOfDayValue(activeEndParam->value(), activeEndCandidate)) {
            request->send(400, "text/plain", "❌ Fehler: active_end muss im Format HH:MM zwischen 00:00 und 23:59 liegen.");
            return;
        }

        bool autoModeCandidate = autoDisplayMode;
        bool autoModeProvided = false;
        if (request->hasParam("auto_mode", true)) {
            autoModeProvided = true;
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

        char randomPoolCandidate[RANDOM_SYMBOL_POOL_LENGTH] = {};
        size_t randomPoolWriteIndex = 0;
        for (size_t index = 0; index < randomPoolParam.length() && randomPoolWriteIndex < RANDOM_SYMBOL_POOL_LENGTH - 1; ++index) {
            char current = static_cast<char>(std::toupper(static_cast<unsigned char>(randomPoolParam.charAt(index))));
            if (current == '*') {
                continue;
            }
            if (!isSupportedLetter(current)) {
                continue;
            }
            bool duplicate = false;
            for (size_t existing = 0; existing < randomPoolWriteIndex; ++existing) {
                if (randomPoolCandidate[existing] == current) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                randomPoolCandidate[randomPoolWriteIndex++] = current;
            }
        }
        if (randomPoolWriteIndex == 0) {
            randomPoolCandidate[0] = '#';
            randomPoolCandidate[1] = '&';
        }

        display_brightness = static_cast<int>(brightnessCandidate);
        display.setBrightness(display_brightness);
        if (!triggerActive && wifiSymbolVisible) {
            display.display();
        }
        letter_display_time = letterTimeCandidate;
        letter_auto_display_interval = autoIntervalCandidate;
        standalone_active_start_minutes = activeStartCandidate;
        standalone_active_end_minutes = activeEndCandidate;
        strncpy(random_symbol_pool, randomPoolCandidate, sizeof(random_symbol_pool));
        random_symbol_pool[sizeof(random_symbol_pool) - 1] = '\0';
        if (autoModeProvided) {
            autoDisplayMode = autoModeCandidate;
        }

        saveConfig();

        String responseMessage = F("✅ Anzeigeeinstellungen gespeichert!");
        if (autoModeProvided) {
            responseMessage += F(" Automodus: ");
            responseMessage += (autoDisplayMode ? F("aktiviert.") : F("deaktiviert."));
        } else {
            responseMessage += F(" Automodus unverändert.");
        }
        responseMessage += F(" Aktivzeit ");
        responseMessage += formatMinutesAsTime(standalone_active_start_minutes);
        responseMessage += F(" bis ");
        responseMessage += formatMinutesAsTime(standalone_active_end_minutes);
        responseMessage += F(".");

        request->send(200, "text/plain", responseMessage);
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
                uint8_t parsedColorModes[NUM_TRIGGERS][NUM_DAYS];
                uint16_t parsedPaletteMasks[NUM_TRIGGERS][NUM_DAYS];
                unsigned long parsedDelays[NUM_TRIGGERS][NUM_DAYS];

                for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
                    for (size_t day = 0; day < NUM_DAYS; ++day) {
                        parsedColorModes[trigger][day] = static_cast<uint8_t>(LetterColorMode::Fixed);
                        uint16_t fullMask = 0;
                        for (size_t paletteIndex = 0; paletteIndex < RANDOM_COLOR_PALETTE_SIZE; ++paletteIndex) {
                            fullMask |= static_cast<uint16_t>(1U << paletteIndex);
                        }
                        parsedPaletteMasks[trigger][day] = fullMask;
                    }
                }

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
                            validationMessage = F("Ungültige Zeichenliste für Tag ");
                            validationMessage += DAY_KEYS[day];
                            break;
                        }

                        for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
                            JsonVariantConst letterVariant = dayLetters[trigger];
                            const char *letterRaw = letterVariant.as<const char *>();
                            if (letterRaw == nullptr) {
                                validationFailed = true;
                                validationMessage = F("Zeichen/Symbol fehlt für Trigger ");
                                validationMessage += String(trigger + 1);
                                validationMessage += F(" am Tag ");
                                validationMessage += DAY_KEYS[day];
                                break;
                            }

                            String letterValue = letterRaw;
                            letterValue.trim();
                            if (letterValue.length() != 1) {
                                validationFailed = true;
                                validationMessage = F("Auswahl muss genau ein Zeichen/Symbol besitzen (Tag ");
                                validationMessage += DAY_KEYS[day];
                                validationMessage += F(", Trigger ");
                                validationMessage += String(trigger + 1);
                                validationMessage += F(").");
                                break;
                            }

                            const char letterChar = letterValue.charAt(0);
                            if (!isSupportedLetter(letterChar)) {
                                validationFailed = true;
                                validationMessage = F("Ungültiges Zeichen/Symbol für Trigger ");
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

                JsonObjectConst colorModesObject = payload["color_modes"].as<JsonObjectConst>();
                if (colorModesObject.isNull()) {
                    colorModesObject = payload["colorModes"].as<JsonObjectConst>();
                }
                if (!validationFailed && !colorModesObject.isNull()) {
                    for (size_t day = 0; day < NUM_DAYS && !validationFailed; ++day) {
                        JsonArrayConst dayModes = colorModesObject[DAY_KEYS[day]].as<JsonArrayConst>();
                        if (dayModes.isNull() || dayModes.size() != NUM_TRIGGERS) {
                            validationFailed = true;
                            validationMessage = F("Ungültige Farbmodus-Liste für Tag ");
                            validationMessage += DAY_KEYS[day];
                            break;
                        }

                        for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
                            const char *modeRaw = dayModes[trigger].as<const char *>();
                            if (modeRaw == nullptr ||
                                !parseLetterColorModeValue(String(modeRaw), parsedColorModes[trigger][day])) {
                                validationFailed = true;
                                validationMessage = F("Ungültiger Farbmodus für Trigger ");
                                validationMessage += String(trigger + 1);
                                validationMessage += F(" am Tag ");
                                validationMessage += DAY_KEYS[day];
                                break;
                            }
                        }
                    }
                }

                JsonObjectConst paletteMasksObject = payload["color_palette_masks"].as<JsonObjectConst>();
                if (paletteMasksObject.isNull()) {
                    paletteMasksObject = payload["colorPaletteMasks"].as<JsonObjectConst>();
                }
                if (!validationFailed && !paletteMasksObject.isNull()) {
                    for (size_t day = 0; day < NUM_DAYS && !validationFailed; ++day) {
                        JsonArrayConst dayMasks = paletteMasksObject[DAY_KEYS[day]].as<JsonArrayConst>();
                        if (dayMasks.isNull() || dayMasks.size() != NUM_TRIGGERS) {
                            validationFailed = true;
                            validationMessage = F("Ungültige Zufallspalette für Tag ");
                            validationMessage += DAY_KEYS[day];
                            break;
                        }

                        for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
                            JsonVariantConst maskVariant = dayMasks[trigger];
                            if (!(maskVariant.is<unsigned int>() || maskVariant.is<int>() || maskVariant.is<long>() || maskVariant.is<unsigned long>())) {
                                validationFailed = true;
                                validationMessage = F("Ungültige Zufallspalette für Trigger ");
                                validationMessage += String(trigger + 1);
                                validationMessage += F(" am Tag ");
                                validationMessage += DAY_KEYS[day];
                                break;
                            }

                            unsigned long maskValue = maskVariant.as<unsigned long>();
                            if (maskValue > 0xFFFFUL) {
                                validationFailed = true;
                                validationMessage = F("Zufallspalette außerhalb des gueltigen Bereichs.");
                                break;
                            }

                            parsedPaletteMasks[trigger][day] = static_cast<uint16_t>(maskValue);
                        }
                    }
                }

                if (!validationFailed) {
                    for (size_t trigger = 0; trigger < NUM_TRIGGERS && !validationFailed; ++trigger) {
                        for (size_t day = 0; day < NUM_DAYS; ++day) {
                            if (parsedColorModes[trigger][day] == static_cast<uint8_t>(LetterColorMode::RandomSelected) &&
                                parsedPaletteMasks[trigger][day] == 0U) {
                                validationFailed = true;
                                validationMessage = F("Zufall (ausgewaehlt) benoetigt mindestens eine Farbe.");
                                break;
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
                        dailyLetterColorModes[trigger][day] = parsedColorModes[trigger][day];
                        dailyLetterRandomPaletteMasks[trigger][day] = parsedPaletteMasks[trigger][day];
                        letter_trigger_delays[trigger][day] = parsedDelays[trigger][day];
                    }
                }

                saveConfig();
                refreshWiFiIdleTimer(F("POST /updateAllLetters JSON"));
                Serial.println(F("✅ JSON-Update: Zeichen/Symbole, Farben, Farbmodi & Verzögerungen übernommen."));
                cleanup();
                sendJsonStatus(request, 200, "ok", F("Zeichen/Symbole, Farben, Farbmodi & Verzögerungen gespeichert."));
                return;
            }

            char parsedLetters[NUM_TRIGGERS][NUM_DAYS];
            char parsedColors[NUM_TRIGGERS][NUM_DAYS][COLOR_STRING_LENGTH];
            uint8_t parsedColorModes[NUM_TRIGGERS][NUM_DAYS];
            uint16_t parsedPaletteMasks[NUM_TRIGGERS][NUM_DAYS];
            unsigned long parsedDelays[NUM_TRIGGERS][NUM_DAYS];
            memcpy(parsedDelays, letter_trigger_delays, sizeof(parsedDelays));

            for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
                for (size_t day = 0; day < NUM_DAYS; ++day) {
                    parsedColorModes[trigger][day] = static_cast<uint8_t>(LetterColorMode::Fixed);
                    uint16_t fullMask = 0;
                    for (size_t paletteIndex = 0; paletteIndex < RANDOM_COLOR_PALETTE_SIZE; ++paletteIndex) {
                        fullMask |= static_cast<uint16_t>(1U << paletteIndex);
                    }
                    parsedPaletteMasks[trigger][day] = fullMask;
                }
            }

            bool success = true;
            String errorMessage;

            for (size_t trigger = 0; trigger < NUM_TRIGGERS && success; ++trigger) {
                for (size_t day = 0; day < NUM_DAYS; ++day) {
                    String letterParam = "letter_" + String(trigger) + "_" + String(day);
                    String colorParam = "color_" + String(trigger) + "_" + String(day);
                    String colorModeParam = "color_mode_" + String(trigger) + "_" + String(day);

                    if (!request->hasParam(letterParam, true) ||
                        !request->hasParam(colorParam, true) ||
                        !request->hasParam(colorModeParam, true)) {
                        success = false;
                        errorMessage = F("Nicht alle Zeichen-/Symbol-, Farb- oder Farbmodus-Felder wurden uebermittelt.");
                        break;
                    }

                    String letterValue = request->getParam(letterParam, true)->value();
                    letterValue.trim();
                    if (letterValue.length() != 1) {
                        success = false;
                        errorMessage = F("Auswahl muss genau ein Zeichen/Symbol besitzen.");
                        break;
                    }

                    const char letterChar = letterValue.charAt(0);
                    if (!isSupportedLetter(letterChar)) {
                        success = false;
                        errorMessage = F("Ungültiges Zeichen/Symbol im Formular.");
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

                    uint8_t parsedMode = 0;
                    if (!parseLetterColorModeValue(request->getParam(colorModeParam, true)->value(), parsedMode)) {
                        success = false;
                        errorMessage = F("Ungueltiger Farbmodus im Formular.");
                        break;
                    }

                    parsedColorModes[trigger][day] = parsedMode;
                    if (parsedMode == static_cast<uint8_t>(LetterColorMode::RandomSelected)) {
                        uint16_t paletteMask = 0;
                        for (size_t paletteIndex = 0; paletteIndex < RANDOM_COLOR_PALETTE_SIZE; ++paletteIndex) {
                            String paletteField =
                                "palette_" + String(trigger) + "_" + String(day) + "_" + String(paletteIndex);
                            if (request->hasParam(paletteField, true)) {
                                paletteMask |= static_cast<uint16_t>(1U << paletteIndex);
                            }
                        }

                        if (paletteMask == 0U) {
                            success = false;
                            errorMessage = F("Zufall (ausgewaehlt) benoetigt mindestens eine Farbe.");
                            break;
                        }

                        parsedPaletteMasks[trigger][day] = paletteMask;
                    }
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
                    dailyLetterColorModes[trigger][day] = parsedColorModes[trigger][day];
                    dailyLetterRandomPaletteMasks[trigger][day] = parsedPaletteMasks[trigger][day];
                    letter_trigger_delays[trigger][day] = parsedDelays[trigger][day];
                }
            }

            saveConfig();
            refreshWiFiIdleTimer(F("POST /updateAllLetters Formular"));
            if (expectDelays) {
                Serial.println(F("✅ Formular-Update: Zeichen/Symbole, Farben, Farbmodi & Verzögerungen gespeichert."));
            } else {
                Serial.println(F("✅ Formular-Update: Zeichen/Symbole, Farben & Farbmodi gespeichert."));
            }
            cleanup();

            String confirmation = expectDelays ? String(F("✅ Zeichen/Symbole, Farben, Farbmodi & Verzoegerungen gespeichert!"))
                                               : String(F("✅ Zeichen/Symbole, Farben & Farbmodi gespeichert!"));
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
            request->send(400, "text/plain", "❌ Fehler: Auswahl muss genau ein Zeichen/Symbol sein!");
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
            request->send(200, "text/plain", "✅ Zeichen/Symbol " + letter + " für Trigger " + String(triggerIndex + 1) + " angezeigt!");
            return;
        }

        int statusCode = 500;
        String errorMessage = F("❌ Fehler: Anzeige fehlgeschlagen.");

        switch (lastDisplayLetterError) {
            case DisplayLetterError::TriggerAlreadyActive:
                statusCode = 409;
                errorMessage = F("❌ Fehler: Bereits aktives Zeichen/Symbol verhindert neue Anzeige!");
                break;
            case DisplayLetterError::LetterNotFound:
                statusCode = 422;
                errorMessage = F("❌ Fehler: Kein Muster für das gewünschte Zeichen/Symbol gefunden!");
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
        const int customIndex = customSymbolIndexFromChar(todayLetter);
        const bool customAvailable =
            customIndex >= 0 &&
            customIndex < static_cast<int>(CUSTOM_SYMBOL_COUNT) &&
            customSymbolEnabled[customIndex] == 1;
        if (todayLetter != '*' && !customAvailable && letterData.find(todayLetter) == letterData.end()) {
            request->send(500, "text/plain", "❌ Fehler: Kein Muster für das heutige Zeichen/Symbol vorhanden!");
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

        String response = "✅ Zeichen-/Symbol-Trigger für Trigger " + String(triggerIndex + 1) + " eingeplant!";
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

    server.onNotFound([](AsyncWebServerRequest *request) {
        if (request->method() == HTTP_OPTIONS) {
            request->send(204, "text/plain", "");
            return;
        }
        request->send(404, "text/plain", "Not found");
    });

    server.begin();
    webServerRunning = true;
    Serial.println(F("✅ Webserver gestartet und Listener aktiv."));
}
