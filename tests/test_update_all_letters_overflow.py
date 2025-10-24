from __future__ import annotations

from pathlib import Path


def _extract_update_all_letters_handler() -> str:
    code = Path("src/web_manager.cpp").read_text(encoding="utf-8")
    anchor = code.find("\"/updateAllLetters\"")
    assert anchor != -1, "Pfad /updateAllLetters nicht gefunden"
    start = code.rfind("server.on", 0, anchor)
    assert start != -1, "server.on-Aufruf nicht gefunden"

    paren_start = code.find("(", start)
    assert paren_start != -1, "Parameterliste des Handler-Aufrufs nicht gefunden"

    depth = 0
    end = None
    for idx in range(paren_start, len(code)):
        char = code[idx]
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                end = idx
                break

    assert end is not None, "Ende des Handler-Aufrufs nicht gefunden"

    semicolon = code.find(";", end)
    assert semicolon != -1, "Abschließendes Semikolon nicht gefunden"
    return code[start:semicolon + 1]


def _extract_body_callback(handler: str) -> str:
    target = "[](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)"
    start = handler.find(target)
    assert start != -1, "Body-Callback für /updateAllLetters nicht gefunden"
    start = handler.find("{", start)
    assert start != -1, "Beginn des Body-Callbacks nicht gefunden"

    depth = 0
    for idx in range(start, len(handler)):
        char = handler[idx]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return handler[start + 1 : idx]

    raise AssertionError("Ende des Body-Callbacks nicht gefunden")


def test_chunked_request_larger_than_limit_results_in_overflow() -> None:
    handler = _extract_update_all_letters_handler()
    body_callback = _extract_body_callback(handler)

    assert "context->overflow = true;" in body_callback, "Overflow-Flag wird nicht gesetzt"
    overflow_check_pos = body_callback.index("context->body.length() + len > MAX_JSON_BODY_SIZE")
    loop_pos = body_callback.index("for (size_t idx = 0; idx < len; ++idx)")
    assert overflow_check_pos < loop_pos, "Overflow-Prüfung erfolgt erst nach dem Append-Loop"
    assert "return;" in body_callback[overflow_check_pos:loop_pos], "Overflow-Abbruch fehlt"
    assert "context->body.reserve(std::min(MAX_JSON_BODY_SIZE, total) + 1);" in body_callback

    assert "sendJsonStatus(request, 413" in handler, "HTTP 413 wird für übergroße JSON-Bodies nicht gesendet"

