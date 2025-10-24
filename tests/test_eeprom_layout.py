from __future__ import annotations

import re
from pathlib import Path


def _parse_constants() -> dict[str, int]:
    header = Path("src/config.h").read_text(encoding="utf-8")
    pattern = re.compile(r"static constexpr [^=]+ ([A-Za-z0-9_]+) = ([^;]+);")
    sizeof_map = {
        "int": 4,
        "unsigned long": 4,
        "uint8_t": 1,
    }

    constants: dict[str, int] = {}

    for name, expr in pattern.findall(header):
        cleaned = expr
        for key, value in sizeof_map.items():
            cleaned = cleaned.replace(f"sizeof({key})", str(value))

        # Evaluate simple arithmetic (addition/multiplication) using already parsed constants.
        value = eval(cleaned, {}, constants)  # noqa: S307 - Ausdruck ist auf bekannte Konstanten beschränkt
        constants[name] = int(value)

    return constants


def test_new_letter_matrix_offsets() -> None:
    constants = _parse_constants()

    assert constants["NUM_TRIGGERS"] == 3
    assert constants["NUM_DAYS"] == 7
    assert constants["COLOR_STRING_LENGTH"] == 8

    letters_size = constants["NUM_TRIGGERS"] * constants["NUM_DAYS"]
    letters_end = constants["EEPROM_OFFSET_DAILY_LETTERS"] + letters_size
    assert letters_end == constants["EEPROM_OFFSET_DAILY_LETTER_COLORS"], "Farb-Block muss direkt auf Letter-Block folgen"

    colors_size = constants["NUM_TRIGGERS"] * constants["NUM_DAYS"] * constants["COLOR_STRING_LENGTH"]
    colors_end = constants["EEPROM_OFFSET_DAILY_LETTER_COLORS"] + colors_size
    assert colors_end == constants["EEPROM_OFFSET_DISPLAY_BRIGHTNESS"], "Display-Helligkeit muss direkt nach Farb-Block starten"

    matrix_end = constants["EEPROM_OFFSET_TRIGGER_DELAY_MATRIX"] + constants["EEPROM_TRIGGER_DELAY_MATRIX_SIZE"]
    assert matrix_end <= constants["EEPROM_OFFSET_AUTO_INTERVAL"], "Verzögerungsmatrix überschneidet sich mit Auto-Intervall"


def test_eeprom_usage_fits_into_memory() -> None:
    constants = _parse_constants()

    eeprom_size = 512
    wifi_connect_end = constants["EEPROM_OFFSET_WIFI_CONNECT_TIMEOUT"] + 4  # sizeof(int)
    version_end = constants["EEPROM_OFFSET_CONFIG_VERSION"] + 2  # uint16_t
    matrix_end = constants["EEPROM_OFFSET_TRIGGER_DELAY_MATRIX"] + constants["EEPROM_TRIGGER_DELAY_MATRIX_SIZE"]

    assert wifi_connect_end <= eeprom_size
    assert version_end <= eeprom_size
    assert constants["EEPROM_OFFSET_CONFIG_VERSION"] >= wifi_connect_end
    assert constants["EEPROM_OFFSET_CONFIG_VERSION"] >= matrix_end
    assert constants["EEPROM_CONFIG_VERSION"] >= 3
