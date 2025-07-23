# RiddleMatrix

RiddleMatrix is firmware for an ESP8266 microcontroller that drives a 64x64 RGB LED matrix. The display shows one configurable letter for each day of the week. Letters may appear automatically on a schedule or be triggered manually over RS485. WiFi connectivity enables a web interface for configuration and storing settings to EEPROM.

## Hardware Requirements

- **ESP8266 board** (NodeMCU v2 recommended)
- **64x64 RGB LED matrix** with FM6126A driver (1/32 scan)
- **DS1307 RTC module**
- **RS485 transceiver** for external triggers
- Wiring as defined in `config.h`

## WiFi Configuration

1. Open `config.h` and edit `wifi_ssid`, `wifi_password` and `hostname` in `saveConfig()`.
2. Compile and upload the firmware.
3. Once connected to your network, visit `http://<hostname>` and use the web page to store updated credentials in EEPROM.

## Building & Uploading

### Arduino IDE

1. Install libraries: **PxMatrix**, **ESPAsyncWebServer**, **ArduinoJson**, **RTClib**, and **Ticker** (built-in).
2. Select **NodeMCU 1.0 (ESP-12E Module)** under *Tools → Board*.
3. Open `Firmware.ino`, verify, and upload.

### PlatformIO

1. Create a new project for platform `esp8266` and board `nodemcuv2`.
2. Add the above libraries to `platformio.ini`.
3. Copy the source files into the `src` folder.
4. Run `platformio run` to build and `platformio run -t upload` to flash.

## Additional Setup

- Connect the LED matrix pins according to `config.h`.
- Attach the RTC to pins `I2C_SDA` and `I2C_SCL`.
- Wire the RS485 enable pin to `GPIO_RS485_ENABLE`.
- Check the serial console at 19200 baud for debug messages.

Once configured, the firmware automatically displays letters each day and can be managed via the web interface.
