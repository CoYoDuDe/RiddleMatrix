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
void setupWebServer();

#endif
