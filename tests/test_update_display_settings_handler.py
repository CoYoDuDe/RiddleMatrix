from __future__ import annotations

import re
from pathlib import Path


def _extract_update_display_settings_handler() -> str:
    code = Path("src/web_manager.cpp").read_text(encoding="utf-8")
    pattern = re.compile(
        r'server\.on\("/updateDisplaySettings".*?request->send\(200,\s*"text/plain",\s*responseMessage\);\s*\}\);',
        re.S,
    )
    match = pattern.search(code)
    assert match, "Handler-Definition für /updateDisplaySettings nicht gefunden"
    return match.group(0)


def test_update_display_settings_preserves_auto_mode_without_param() -> None:
    handler = _extract_update_display_settings_handler()

    assert "bool autoModeCandidate = autoDisplayMode;" in handler
    assert "bool autoModeProvided = false;" in handler
    assert "if (autoModeProvided) {\n            autoDisplayMode = autoModeCandidate;\n        }" in handler
    assert "Automodus unverändert." in handler
