from __future__ import annotations

import re
from pathlib import Path


def _extract_trigger_letter_handler() -> str:
    code = Path("src/web_manager.cpp").read_text(encoding="utf-8")
    pattern = re.compile(r'server\.on\("/triggerLetter".*?request->send\(200,\s*"text/plain",\s*response\);\s*\}\);', re.S)
    match = pattern.search(code)
    assert match, "Handler-Definition für /triggerLetter nicht gefunden"
    return match.group(0)


def test_trigger_letter_handler_allows_active_display_queueing() -> None:
    handler = _extract_trigger_letter_handler()
    assert "if (triggerActive)" not in handler, "Trigger wird bei aktiver Anzeige weiterhin geblockt"
    assert "enqueuePendingTrigger" in handler, "Trigger wird nicht über die Warteschlange eingeplant"
    assert "Hinweis: Aktuelle Anzeige läuft noch; Ausführung erfolgt anschließend." in handler
    assert "❌ Fehler: Für diesen Trigger ist bereits eine Ausführung geplant!" in handler


def test_trigger_letter_handler_retains_error_fallbacks() -> None:
    handler = _extract_trigger_letter_handler()
    assert "❌ Fehler: Trigger konnte nicht eingeplant werden!" in handler
    assert handler.count("request->send(409") == 1
    assert handler.count("request->send(503") == 1
