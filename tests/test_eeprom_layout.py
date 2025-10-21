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

    letters_end = constants["EEPROM_OFFSET_DAILY_LETTERS"] + constants["NUM_TRIGGERS"] * constants["NUM_DAYS"]
    assert letters_end <= constants["EEPROM_OFFSET_DAILY_LETTER_COLORS"], "Letter-Block überschneidet sich mit Farben"

    colors_end = constants["EEPROM_OFFSET_DAILY_LETTER_COLORS"] + constants["NUM_TRIGGERS"] * constants["NUM_DAYS"] * constants["COLOR_STRING_LENGTH"]
    assert colors_end <= constants["EEPROM_OFFSET_DISPLAY_BRIGHTNESS"], "Farb-Block überschneidet sich mit Display-Parametern"


def test_eeprom_usage_fits_into_memory() -> None:
    constants = _parse_constants()

    eeprom_size = 512
    wifi_connect_end = constants["EEPROM_OFFSET_WIFI_CONNECT_TIMEOUT"] + 4  # sizeof(int)
    version_end = constants["EEPROM_OFFSET_CONFIG_VERSION"] + 2  # uint16_t

    assert wifi_connect_end <= eeprom_size
    assert version_end <= eeprom_size
    assert constants["EEPROM_CONFIG_VERSION"] >= 2
