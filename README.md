# RiddleMatrix

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.
RiddleMatrix is firmware for an ESP8266 microcontroller that drives a 64x64 RGB LED matrix. The display shows one configurable letter for each day of the week. Letters may appear automatically on a schedule or be triggered manually over RS485. WiFi connectivity enables a web interface for configuration and storing settings to EEPROM.

See [TODO.md](TODO.md) for the project roadmap.

## Hardware Requirements

- **ESP8266 board** (NodeMCU v2 recommended)
- **64x64 RGB LED matrix** with FM6126A driver (1/32 scan)
- **DS1307 RTC module**
- **RS485 transceiver** for external triggers
- Wiring as defined in `config.h`

## WiFi Configuration

1. Open `config.h` and edit `wifi_ssid`, `wifi_password`, `hostname` and optionally `wifi_connect_timeout` in `saveConfig()`.
2. Compile and upload the firmware.
3. Once connected to your network, visit `http://<hostname>` and use the web page to store updated credentials in EEPROM.

## Building & Uploading

### Arduino IDE

1. Install libraries: **PxMatrix**, **ESPAsyncWebServer**, **ArduinoJson**, **RTClib**, and **Ticker** (built-in).
2. Select **NodeMCU 1.0 (ESP-12E Module)** under *Tools → Board*.
3. Open `Firmware.ino`, verify, and upload.

### PlatformIO

Install PlatformIO using `pip install platformio` and ensure the `platformio` command is available in your PATH before running `pio run`.

1. PlatformIO is pre-configured using `platformio.ini` and the sources in `src/`.
2. Install the **PxMatrix** library used by `platformio.ini`:
   - Online: run `pio lib install 2dom/PxMatrix`.
   - Offline: clone the [PxMatrix repository](https://github.com/2dom/PxMatrix)
     and place it inside the `lib/` directory.
3. Run `pio run` to build the firmware.
4. Use `pio run -t upload` to flash it to the board.

## Additional Setup

- Connect the LED matrix pins according to `config.h`.
- Attach the RTC to pins `I2C_SDA` and `I2C_SCL`.
- Wire the RS485 enable pin to `GPIO_RS485_ENABLE`.
- Check the serial console at 19200 baud for debug messages.

Once configured, the firmware automatically displays letters each day and can be managed via the web interface.

## Configuration

The `config.h` file contains placeholder WiFi credentials used when no
settings are stored in EEPROM. Real network credentials should **not** be
committed to the repository. Instead, provide them through the initial EEPROM
setup or via the device's configuration screen.
The `wifi_connect_timeout` setting controls how long the device will attempt to
connect to WiFi before giving up. The default is 30 seconds.
