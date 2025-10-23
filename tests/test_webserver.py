from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

import pytest

try:  # pragma: no cover - optional Abhängigkeit
    import flask  # noqa: F401
except ModuleNotFoundError:  # pragma: no cover
    pytest.skip("Flask nicht installiert – Webserver-Tests werden übersprungen", allow_module_level=True)


def _load_webserver(tmp_path):
    module_name = "webserver_for_tests"
    module_path = Path(__file__).resolve().parents[1] / "USBStick-Setup/files/usr/local/bin/webserver.py"

    sys.modules.pop(module_name, None)
    spec = importlib.util.spec_from_file_location(module_name, module_path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[module_name] = module
    assert spec.loader is not None
    spec.loader.exec_module(module)

    module.CONFIG_FILE = str(tmp_path / "config.json")
    module.save_config({"boxen": {}, "boxOrder": []})
    module.app.config["TESTING"] = True
    return module


def _empty_box(module, ip="1.2.3.4"):
    letters = {day: ["" for _ in range(module.TRIGGER_SLOTS)] for day in module.DAYS}
    colors = {day: [module.DEFAULT_COLOR for _ in range(module.TRIGGER_SLOTS)] for day in module.DAYS}
    delays = {day: [module.DEFAULT_DELAY for _ in range(module.TRIGGER_SLOTS)] for day in module.DAYS}
    return {"ip": ip, "letters": letters, "colors": colors, "delays": delays}


@pytest.fixture
def webserver_app(tmp_path):
    module = _load_webserver(tmp_path)
    with module.app.test_client() as client:
        yield module, client


def test_get_hostname_from_web_supports_attribute_variants(webserver_app, monkeypatch):
    module, _client = webserver_app

    class FakeResponse:
        def __init__(self, text: str, ok: bool = True) -> None:
            self.text = text
            self.ok = ok

    html_variants = [
        "<html><body><input type=\"text\" name=\"hostname\" value=\"BoxAlpha\"></body></html>",
        "<html><body><form><input value=' BoxBeta ' data-extra=\"1\" name='hostname' type='text'></form></body></html>",
        "<html><body><input type='text' name='hostname'></body></html>",
    ]

    call_state = {"index": 0}

    def fake_get(url, *args, **kwargs):
        index = call_state["index"]
        if index >= len(html_variants):
            pytest.fail("Unerwarteter zusätzlicher Aufruf von requests.get")
        call_state["index"] += 1
        return FakeResponse(html_variants[index])

    monkeypatch.setattr(module.requests, "get", fake_get)

    assert module.get_hostname_from_web("1.2.3.4") == "BoxAlpha"
    assert module.get_hostname_from_web("5.6.7.8") == " BoxBeta "
    assert module.get_hostname_from_web("9.8.7.6") == "Unbekannt"


def test_transfer_box_returns_json_on_get_error(webserver_app, monkeypatch):
    module, client = webserver_app
    module.save_config({"boxen": {"TestBox": _empty_box(module)}, "boxOrder": []})

    def raise_request(*args, **kwargs):
        raise module.requests.RequestException("boom")

    monkeypatch.setattr(module.requests, "get", raise_request)

    response = client.get("/transfer_box", query_string={"hostname": "TestBox"})
    assert response.status_code == 200
    assert response.get_json() == {"status": "❌ Box nicht erreichbar"}


def test_transfer_box_returns_json_on_post_error(webserver_app, monkeypatch):
    module, client = webserver_app
    box = _empty_box(module)
    box["letters"]["mo"][0] = "A"
    module.save_config({"boxen": {"TestBox": box}, "boxOrder": []})

    class FakeResponse:
        def __init__(self, text: str = "", ok: bool = True, json_data=None) -> None:
            self.text = text
            self.ok = ok
            self._json = json_data

        def json(self):
            if self._json is None:
                raise ValueError("No JSON data")
            return self._json

    def fake_get(url, *args, **kwargs):
        if url.endswith("/api/trigger-delays"):
            delays = {day: [module.DEFAULT_DELAY for _ in range(module.TRIGGER_SLOTS)] for day in module.DAYS}
            return FakeResponse(ok=True, json_data={"delays": delays})
        return FakeResponse("", True)

    monkeypatch.setattr(module.requests, "get", fake_get)

    def raise_post(*args, **kwargs):
        raise module.requests.RequestException("boom")

    monkeypatch.setattr(module.requests, "post", raise_post)

    response = client.get("/transfer_box", query_string={"hostname": "TestBox"})
    assert response.status_code == 200
    assert response.get_json() == {"status": "❌ Fehler bei Übertragung"}


def test_update_box_updates_specific_trigger(webserver_app):
    module, client = webserver_app
    module.save_config({"boxen": {"TestBox": _empty_box(module)}, "boxOrder": []})

    response = client.post(
        "/update_box",
        json={"hostname": "TestBox", "day": "mo", "triggerIndex": 1, "letter": "B"},
    )

    assert response.status_code == 200
    assert response.get_json() == {"status": "success"}

    config = module.load_config()
    assert config["boxen"]["TestBox"]["letters"]["mo"][1] == "B"

    response = client.post(
        "/update_box",
        json={"hostname": "TestBox", "day": "mo", "triggerIndex": 2, "color": "#123456"},
    )

    assert response.status_code == 200
    config = module.load_config()
    assert config["boxen"]["TestBox"]["colors"]["mo"][2] == "#123456"

    response = client.post(
        "/update_box",
        json={"hostname": "TestBox", "day": "mo", "triggerIndex": 0, "delay": 1.25},
    )

    assert response.status_code == 200
    config = module.load_config()
    assert config["boxen"]["TestBox"]["delays"]["mo"][0] == module._coerce_delay_value(1.25)


def test_transfer_box_sends_all_triggers_json(webserver_app, monkeypatch):
    module, client = webserver_app
    letters = {day: [f"{day}{idx}" for idx in range(module.TRIGGER_SLOTS)] for day in module.DAYS}
    colors = {day: [f"#0{idx}{idx}{idx}{idx}{idx}"[:7] for idx in range(module.TRIGGER_SLOTS)] for day in module.DAYS}
    delays = {day: [idx for idx in range(module.TRIGGER_SLOTS)] for day in module.DAYS}
    module.save_config({"boxen": {"TestBox": {"ip": "1.2.3.4", "letters": letters, "colors": colors, "delays": delays}}, "boxOrder": []})

    html_parts = ["<html><body>"]
    for index, day in enumerate(module.DAYS):
        for slot in range(module.TRIGGER_SLOTS):
            if index % 2 == 0:
                select_name = f"letter_{slot}_{index}"
                color_name = f"color_{slot}_{index}"
            elif slot == 0:
                select_name = f"letter{index}"
                color_name = f"color{index}"
            elif slot % 2 == 0:
                select_name = f"letter_{index}_{slot}"
                color_name = f"color_{index}_{slot}"
            else:
                select_name = f"letter{index}_{slot}"
                color_name = f"color{index}_{slot}"

            html_parts.append(
                f"<select name='{select_name}'><option value='x' selected>x</option></select>"
            )
            html_parts.append(
                f"<input name='{color_name}' value='{module.DEFAULT_COLOR}'>"
            )
    html_parts.append("</body></html>")
    fake_html = "".join(html_parts)

    class FakeResponse:
        def __init__(self, text: str = "", ok: bool = True, json_data=None) -> None:
            self.text = text
            self.ok = ok
            self._json = json_data

        def json(self):
            if self._json is None:
                raise ValueError("No JSON data")
            return self._json

    def fake_get(url, *args, **kwargs):
        if url.endswith("/api/trigger-delays"):
            return FakeResponse(ok=True, json_data={"delays": {day: [0 for _ in range(module.TRIGGER_SLOTS)] for day in module.DAYS}})
        return FakeResponse(fake_html, True)

    monkeypatch.setattr(module.requests, "get", fake_get)

    captured = {}

    def fake_post(url, json=None, timeout=3):
        captured["url"] = url
        captured["json"] = json

        class PostResponse:
            ok = True

        return PostResponse()

    monkeypatch.setattr(module.requests, "post", fake_post)

    response = client.get("/transfer_box", query_string={"hostname": "TestBox"})
    assert response.status_code == 200
    assert response.get_json() == {"status": "✅ Übertragen"}
    assert captured["url"].endswith("/updateAllLetters")
    assert captured["json"] == {"letters": letters, "colors": colors, "delays": delays}


def test_transfer_box_coerces_decimal_delays_to_integers(webserver_app, monkeypatch):
    module, client = webserver_app
    letters = {day: ["" for _ in range(module.TRIGGER_SLOTS)] for day in module.DAYS}
    colors = {day: [module.DEFAULT_COLOR for _ in range(module.TRIGGER_SLOTS)] for day in module.DAYS}
    decimal_template = [0.4, 1.6, 2.5]
    delays = {day: list(decimal_template) for day in module.DAYS}
    module.save_config(
        {"boxen": {"TestBox": {"ip": "1.2.3.4", "letters": letters, "colors": colors, "delays": delays}}, "boxOrder": []}
    )

    class FakeResponse:
        def __init__(self, text: str = "", ok: bool = True, json_data=None) -> None:
            self.text = text
            self.ok = ok
            self._json = json_data

        def json(self):
            if self._json is None:
                raise ValueError("No JSON data")
            return self._json

    def fake_get(url, *args, **kwargs):
        if url.endswith("/api/trigger-delays"):
            return FakeResponse(ok=True, json_data={"delays": {day: [0 for _ in range(module.TRIGGER_SLOTS)] for day in module.DAYS}})
        return FakeResponse("<html></html>", True)

    monkeypatch.setattr(module.requests, "get", fake_get)

    captured = {}

    def fake_post(url, json=None, timeout=3):
        captured["url"] = url
        captured["json"] = json

        class PostResponse:
            ok = True

        return PostResponse()

    monkeypatch.setattr(module.requests, "post", fake_post)

    response = client.get("/transfer_box", query_string={"hostname": "TestBox"})
    assert response.status_code == 200
    assert response.get_json() == {"status": "✅ Übertragen"}
    assert "json" in captured

    expected_delays = {
        day: [module._coerce_delay_value(value) for value in decimal_template] for day in module.DAYS
    }

    assert captured["json"]["delays"] == expected_delays
    for day in module.DAYS:
        assert all(isinstance(value, int) for value in captured["json"]["delays"][day])


def test_transfer_box_matches_firmware_day_index_mapping(webserver_app, monkeypatch):
    module, client = webserver_app

    letters = {day: ["" for _ in range(module.TRIGGER_SLOTS)] for day in module.DAYS}
    colors = {day: [module.DEFAULT_COLOR for _ in range(module.TRIGGER_SLOTS)] for day in module.DAYS}
    delays = {day: [module.DEFAULT_DELAY for _ in range(module.TRIGGER_SLOTS)] for day in module.DAYS}

    firmware_order = sorted(module.DAY_TO_FIRMWARE_INDEX.items(), key=lambda item: item[1])
    html_parts = ["<html><body>"]
    for day_name, firmware_index in firmware_order:
        for slot in range(module.TRIGGER_SLOTS):
            letter_value = f"L{day_name.upper()}{slot}"
            color_value = f"#{firmware_index}{slot}{firmware_index}{slot}{firmware_index}{slot}"
            delay_value = firmware_index * 10 + slot

            letters[day_name][slot] = letter_value
            colors[day_name][slot] = color_value
            delays[day_name][slot] = delay_value

            html_parts.append(
                "<select name='"
                + f"letter_{slot}_{firmware_index}"
                + "'><option value='"
                + letter_value
                + "' selected>"
                + letter_value
                + "</option></select>"
            )
            html_parts.append(
                "<input type='color' name='"
                + f"color_{slot}_{firmware_index}"
                + "' value='"
                + color_value
                + "'>"
            )
            html_parts.append(
                "<input type='number' name='"
                + f"delay_{slot}_{firmware_index}"
                + "' value='"
                + str(delay_value)
                + "'>"
            )

    html_parts.append("</body></html>")
    fake_html = "".join(html_parts)

    module.save_config(
        {"boxen": {"TestBox": {"ip": "1.2.3.4", "letters": letters, "colors": colors, "delays": delays}}, "boxOrder": []}
    )

    fake_delays_payload = {"delays": {day: list(delays[day]) for day, _ in firmware_order}}

    class FakeResponse:
        def __init__(self, text: str = "", ok: bool = True, json_data=None) -> None:
            self.text = text
            self.ok = ok
            self._json = json_data

        def json(self):
            if self._json is None:
                raise ValueError("No JSON data")
            return self._json

    def fake_get(url, *args, **kwargs):
        if url.endswith("/api/trigger-delays"):
            return FakeResponse(ok=True, json_data=fake_delays_payload)
        return FakeResponse(fake_html, True)

    monkeypatch.setattr(module.requests, "get", fake_get)

    post_calls = []

    def fake_post(*args, **kwargs):
        post_calls.append((args, kwargs))

        class PostResponse:
            ok = True

        return PostResponse()

    monkeypatch.setattr(module.requests, "post", fake_post)

    response = client.get("/transfer_box", query_string={"hostname": "TestBox"})
    assert response.status_code == 200
    assert response.get_json() == {"status": "⏭️ Bereits aktuell"}
    assert post_calls == []


def test_extract_box_state_supports_trigger_first_schema(webserver_app):
    module, _client = webserver_app

    expected_letters = {day: ["" for _ in range(module.TRIGGER_SLOTS)] for day in module.DAYS}
    expected_colors = {
        day: [module.DEFAULT_COLOR for _ in range(module.TRIGGER_SLOTS)] for day in module.DAYS
    }

    html_parts = ["<html><body>"]
    for day_index, day in enumerate(module.DAYS):
        for slot in range(module.TRIGGER_SLOTS):
            letter_value = f"L{day_index}{slot}"
            color_value = f"#{slot}{day_index}{slot}{day_index}{slot}{day_index}"
            expected_letters[day][slot] = letter_value
            expected_colors[day][slot] = color_value

            if (day_index + slot) % 2 == 0:
                select_name = f"letter_{slot}_{day_index}"
                color_name = f"color_{slot}_{day_index}"
            else:
                select_name = f"letter{slot}{day_index}"
                color_name = f"color{slot}{day_index}"

            html_parts.append(
                "<select name='"
                + select_name
                + "'><option value='"
                + letter_value
                + "' selected>"
                + letter_value
                + "</option></select>"
            )
            html_parts.append(
                f"<input name='{color_name}' value='{color_value}'>"
            )
    html_parts.append("</body></html>")

    soup = module.BeautifulSoup("".join(html_parts), "html.parser")
    letters, colors = module.extract_box_state_from_soup(soup)

    assert letters == expected_letters
    assert colors == expected_colors


def test_fetch_trigger_delays_returns_numeric_matrix(webserver_app, monkeypatch):
    module, _ = webserver_app

    payload = {
        "delays": {
            "mo": [0, 1, 2.5],
            "di": {"0": "3", "1": 4.75, "2": 6},
            "so": [7, 8, 9],
        }
    }

    class FakeResponse:
        ok = True

        @staticmethod
        def json():
            return payload

    def fake_get(url, *args, **kwargs):
        assert url == "http://1.2.3.4/api/trigger-delays"
        return FakeResponse()

    monkeypatch.setattr(module.requests, "get", fake_get)

    delays = module.fetch_trigger_delays("1.2.3.4")
    assert delays is not None

    assert delays["mo"] == [module._coerce_delay_value(value) for value in payload["delays"]["mo"]]
    assert delays["di"] == [module._coerce_delay_value(payload["delays"]["di"].get(str(idx))) for idx in range(module.TRIGGER_SLOTS)]
    assert delays["mi"] == [module.DEFAULT_DELAY for _ in range(module.TRIGGER_SLOTS)]
    assert delays["so"] == [module._coerce_delay_value(value) for value in payload["delays"]["so"]]

def test_migrate_config_adds_delay_matrix(webserver_app):
    module, _ = webserver_app
    letters = {day: ["" for _ in range(module.TRIGGER_SLOTS)] for day in module.DAYS}
    colors = {day: [module.DEFAULT_COLOR for _ in range(module.TRIGGER_SLOTS)] for day in module.DAYS}
    legacy_config = {"boxen": {"Legacy": {"ip": "1.2.3.4", "letters": letters, "colors": colors}}, "boxOrder": []}

    module.save_config(legacy_config)
    migrated = module.load_config()

    assert "delays" in migrated["boxen"]["Legacy"]
    for day in module.DAYS:
        assert len(migrated["boxen"]["Legacy"]["delays"][day]) == module.TRIGGER_SLOTS
        assert all(value == module.DEFAULT_DELAY for value in migrated["boxen"]["Legacy"]["delays"][day])
