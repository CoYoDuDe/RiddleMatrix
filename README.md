# RiddleMatrix

**RiddleMatrix** is firmware for an ESP8266 based LED matrix display. It shows a configurable letter for each day of the week. Letters can be triggered manually or automatically at set intervals and are rendered on a 64x64 matrix in different colors. WiFi connectivity allows configuration through a simple web interface.

## Hardware Requirements

- **ESP8266 board** – tested with NodeMCU v2
- **64x64 RGB LED matrix** (FM6126A driver, 1/32 scan)
- **DS1307 RTC module**
- **RS485 transceiver** for external triggers
- Common wiring as defined in `config.h`

## WiFi Configuration

When the device boots it reads WiFi credentials from EEPROM. If none exist, default values from `config.h` are used. Change the defaults before compiling or update them through the web interface once connected.

1. Edit `config.h` and adjust `wifi_ssid`, `wifi_password` and `hostname` in the `saveConfig()` section.
2. Compile and upload the firmware.
3. After the ESP8266 connects to your WiFi network open `http://<hostname>` in a browser to access the configuration page.
4. On the page you can update WiFi data. New values are stored in EEPROM so they persist after reboot.

## Building & Uploading

### Arduino IDE

1. Install the following libraries from the Library Manager:
   - **PxMatrix**
   - **ESPAsyncWebServer**
   - **ArduinoJson**
   - **RTClib**
   - **Ticker** (built-in)
2. Select the board **NodeMCU 1.0 (ESP-12E Module)** under `Tools → Board`.
3. Open `Firmware.ino` in the Arduino IDE and verify it compiles.
4. Upload the sketch to your ESP8266.

### PlatformIO

1. Create a new project for the **esp8266** platform and board `nodemcuv2`.
2. Add the same libraries as listed above to `platformio.ini`.
3. Place all source files from this repository in the project directory.
4. Run `platformio run` to build and `platformio run -t upload` to flash the firmware.

## Additional Setup Steps

- Connect the LED matrix to the pins specified in `config.h`.
- Attach the DS1307 RTC via I²C to pins `I2C_SDA` and `I2C_SCL`.
- The RS485 driver enable pin is `GPIO_RS485_ENABLE`.
- After uploading for the first time, check the serial console at 19200 baud for debugging information.

Once configured the firmware will display the daily letters automatically and can be controlled via the web interface.
