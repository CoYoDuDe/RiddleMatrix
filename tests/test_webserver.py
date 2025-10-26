from __future__ import annotations

import importlib.util
import logging
import sys
from pathlib import Path
import copy
import json

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


def test_load_config_recovers_from_corrupted_file(tmp_path):
    module = _load_webserver(tmp_path)
    config_path = Path(module.CONFIG_FILE)
    config_path.write_text("{", encoding="utf-8")

    data = module.load_config()

    assert data == module._default_config()
    assert json.loads(config_path.read_text(encoding="utf-8")) == module._default_config()


def test_save_config_handles_interrupted_write(tmp_path, monkeypatch):
    module = _load_webserver(tmp_path)
    config_path = Path(module.CONFIG_FILE)

    initial_config = {"boxen": {"Alt": _empty_box(module)}, "boxOrder": ["Alt"]}
    module.save_config(initial_config)

    original_replace = module.os.replace
    def failing_replace(src, dst):
        raise RuntimeError("simulierte Unterbrechung während os.replace")

    monkeypatch.setattr(module.os, "replace", failing_replace)

    new_config = {"boxen": {"Neu": _empty_box(module)}, "boxOrder": ["Neu"]}

    with pytest.raises(RuntimeError):
        module.save_config(new_config)

    content_after_failure = config_path.read_text(encoding="utf-8")
    assert content_after_failure.strip() != "", "Konfigurationsdatei darf nicht leer sein"
    assert json.loads(content_after_failure) == initial_config

    # Temporäre Dateien müssen entfernt werden
    temp_files_remaining = [
        p
        for p in config_path.parent.iterdir()
        if p.is_file() and p.name.startswith(".config-") and p.suffix == ".tmp"
    ]
    assert not temp_files_remaining

    # Nach Wiederanlauf (ohne Fehler) muss die neue Konfiguration vollständig geschrieben werden
    monkeypatch.setattr(module.os, "replace", original_replace)
    module.save_config(new_config)

    final_content = config_path.read_text(encoding="utf-8")
    assert final_content.strip() != ""
    assert json.loads(final_content) == new_config

@pytest.mark.parametrize("payload", [[], None])
def test_load_config_repairs_non_dict_payload(tmp_path, payload):
    module = _load_webserver(tmp_path)
    config_path = Path(module.CONFIG_FILE)
    config_path.write_text(json.dumps(payload), encoding="utf-8")

    data = module.load_config()

    assert data == module._default_config()
    assert json.loads(config_path.read_text(encoding="utf-8")) == module._default_config()


def test_load_config_is_idempotent_on_second_call(tmp_path, monkeypatch):
    module = _load_webserver(tmp_path)
    config_path = Path(module.CONFIG_FILE)

    initial_payload = {
        "boxen": {
            "Box Eins": {
                "ip": "192.0.2.1",
            }
        },
        "boxOrder": ["Box Eins"],
    }
    config_path.write_text(json.dumps(initial_payload), encoding="utf-8")

    original_save_config = module.save_config
    save_calls = []

    def spy_save_config(data):
        save_calls.append(json.loads(json.dumps(data)))
        original_save_config(data)

    monkeypatch.setattr(module, "save_config", spy_save_config)

    first_result = module.load_config()
    saved_after_first = json.loads(config_path.read_text(encoding="utf-8"))
    assert saved_after_first == first_result
    assert len(save_calls) >= 1

    save_calls.clear()
    before_second = config_path.read_text(encoding="utf-8")
    second_result = module.load_config()
    after_second = config_path.read_text(encoding="utf-8")

    assert second_result == first_result
    assert after_second == before_second
    assert save_calls == []


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
        assert kwargs.get("allow_redirects") is False
        index = call_state["index"]
        if index >= len(html_variants):
            pytest.fail("Unerwarteter zusätzlicher Aufruf von requests.get")
        call_state["index"] += 1
        return FakeResponse(html_variants[index])

    monkeypatch.setattr(module.requests, "get", fake_get)

    assert module.get_hostname_from_web("1.2.3.4") == "BoxAlpha"
    assert module.get_hostname_from_web("5.6.7.8") == " BoxBeta "
    assert module.get_hostname_from_web("9.8.7.6") == "Unbekannt"


def test_learn_box_sanitizes_hostnames_and_devices_output(webserver_app, monkeypatch):
    module, client = webserver_app

    malicious_hostname = "Box<script>alert(1)</script> _-42"
    sanitized = module.sanitize_hostname(malicious_hostname)
    assert sanitized != ""

    template_box = _empty_box(module, ip="192.0.2.5")

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
        assert kwargs.get("allow_redirects") is False
        if url.endswith("/api/trigger-delays"):
            return FakeResponse(ok=True, json_data={"delays": copy.deepcopy(template_box["delays"])})
        return FakeResponse("<html></html>", True)

    monkeypatch.setattr(module.requests, "get", fake_get)
    monkeypatch.setattr(
        module,
        "extract_box_state_from_soup",
        lambda soup: (
            copy.deepcopy(template_box["letters"]),
            copy.deepcopy(template_box["colors"]),
            copy.deepcopy(template_box["delays"]),
        ),
    )
    monkeypatch.setattr(module, "fetch_trigger_delays", lambda ip: copy.deepcopy(template_box["delays"]))

    module.learn_box(template_box["ip"], malicious_hostname)

    config = module.load_config()
    assert sanitized in config["boxen"]
    assert malicious_hostname not in config["boxen"]
    assert config["boxen"][sanitized]["ip"] == template_box["ip"]

    monkeypatch.setattr(module, "get_connected_devices", lambda: [{"ip": template_box["ip"], "hostname": sanitized}])

    response = client.get("/devices")
    assert response.status_code == 200
    payload = response.get_json()
    assert sanitized in payload["boxen"]
    assert payload["connected"][0]["hostname"] == sanitized
    assert malicious_hostname not in str(payload)


def test_transfer_box_returns_placeholder_status_without_requests_call(webserver_app, monkeypatch):
    module, client = webserver_app

    hostname = "Box Platzhalter"
    config = module.load_config()
    config["boxen"][hostname] = _empty_box(module, ip=module.SAFE_IP_PLACEHOLDER)
    config["boxOrder"] = [hostname]
    module.save_config(config)

    def failing_get(*args, **kwargs):
        raise RuntimeError("requests.get darf für Platzhalter-IP nicht aufgerufen werden")

    monkeypatch.setattr(module.requests, "get", failing_get)

    response = client.post("/transfer_box", json={"hostname": hostname})

    assert response.status_code == 200
    assert response.get_json() == {"status": "❌ IP unbekannt"}


def test_transfer_box_returns_json_on_get_error(webserver_app, monkeypatch):
    module, client = webserver_app
    module.save_config({"boxen": {"TestBox": _empty_box(module)}, "boxOrder": []})

    def raise_request(*args, **kwargs):
        raise module.requests.RequestException("boom")

    monkeypatch.setattr(module.requests, "get", raise_request)

    response = client.post("/transfer_box", json={"hostname": "TestBox"})
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
        assert kwargs.get("allow_redirects") is False
        if url.endswith("/api/trigger-delays"):
            delays = {day: [module.DEFAULT_DELAY for _ in range(module.TRIGGER_SLOTS)] for day in module.DAYS}
            return FakeResponse(ok=True, json_data={"delays": delays})
        return FakeResponse("", True)

    monkeypatch.setattr(module.requests, "get", fake_get)

    def raise_post(*args, **kwargs):
        raise module.requests.RequestException("boom")

    monkeypatch.setattr(module.requests, "post", raise_post)

    response = client.post("/transfer_box", json={"hostname": "TestBox"})
    assert response.status_code == 200
    assert response.get_json() == {"status": "❌ Fehler bei Übertragung"}


def test_transfer_box_rejects_redirects(webserver_app, monkeypatch):
    module, client = webserver_app
    box = _empty_box(module)
    module.save_config({"boxen": {"TestBox": box}, "boxOrder": []})

    class RedirectResponse:
        def __init__(self, status_code=307):
            self.status_code = status_code
            self.headers = {"Location": "http://127.0.0.1:8080/shutdown"}
            self.ok = True
            self.text = ""
            self.is_redirect = True
            self.is_permanent_redirect = False

    def fake_get(url, *args, **kwargs):
        assert kwargs.get("allow_redirects") is False
        if url == f"http://{box['ip']}/":
            return RedirectResponse()
        pytest.fail(f"Unerwarteter GET-Aufruf: {url}")

    def fake_post(*args, **kwargs):
        pytest.fail("POST darf bei Redirect nicht aufgerufen werden")

    monkeypatch.setattr(module.requests, "get", fake_get)
    monkeypatch.setattr(module.requests, "post", fake_post)

    response = client.post("/transfer_box", json={"hostname": "TestBox"})

    assert response.status_code == 502
    assert response.get_json() == {
        "status": "❌ Unerwartete Weiterleitung",
        "details": "Box antwortete mit HTTP 307 und Weiterleitung",
    }


def test_transfer_box_blocks_trigger_delay_redirect(webserver_app, monkeypatch):
    module, client = webserver_app

    box = _empty_box(module)
    module.save_config({"boxen": {"TestBox": box}, "boxOrder": []})

    class FakeResponse:
        def __init__(self, text: str = "", status_code: int = 200, *, is_redirect: bool = False) -> None:
            self.text = text
            self.status_code = status_code
            self.ok = status_code == 200
            self.is_redirect = is_redirect
            self.is_permanent_redirect = False

        def json(self):  # pragma: no cover - sollte nicht genutzt werden
            raise AssertionError("json() darf für Redirect-Test nicht aufgerufen werden")

    sample_letters = {day: ["" for _ in range(module.TRIGGER_SLOTS)] for day in module.DAYS}
    sample_colors = {day: [module.DEFAULT_COLOR for _ in range(module.TRIGGER_SLOTS)] for day in module.DAYS}
    sample_delays = {day: [module.DEFAULT_DELAY for _ in range(module.TRIGGER_SLOTS)] for day in module.DAYS}

    def fake_extract(_soup):
        return sample_letters, sample_colors, sample_delays

    call_state = {"trigger": False}

    def fake_get(url, *args, **kwargs):
        assert kwargs.get("allow_redirects") is False
        if url.endswith("/api/trigger-delays"):
            assert not call_state["trigger"], "Trigger-Delays API darf nur einmal abgefragt werden"
            call_state["trigger"] = True
            return FakeResponse(status_code=307, is_redirect=True)
        if url.endswith("/"):
            return FakeResponse("<html></html>")
        pytest.fail(f"Unerwarteter GET-Aufruf: {url}")

    def fake_post(*args, **kwargs):  # pragma: no cover - sollte nicht aufgerufen werden
        pytest.fail("POST darf bei Redirect nicht aufgerufen werden")

    monkeypatch.setattr(module.requests, "get", fake_get)
    monkeypatch.setattr(module.requests, "post", fake_post)
    monkeypatch.setattr(module, "extract_box_state_from_soup", fake_extract)

    response = client.post("/transfer_box", json={"hostname": "TestBox"})

    assert call_state["trigger"] is True
    assert response.status_code == 502
    assert response.get_json() == {
        "status": "❌ Unerwartete Weiterleitung",
        "details": "Trigger-Delay-Abfrage (1.2.3.4) lieferte HTTP 307 mit Weiterleitung",
    }


def test_transfer_box_allows_remote_clients(webserver_app, monkeypatch):
    module, client = webserver_app
    box = _empty_box(module)
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
        assert kwargs.get("allow_redirects") is False
        if url.endswith("/api/trigger-delays"):
            return FakeResponse(ok=True, json_data={"delays": box["delays"]})
        return FakeResponse("<html></html>", True)

    monkeypatch.setattr(module.requests, "get", fake_get)
    monkeypatch.setattr(module.requests, "post", lambda *args, **kwargs: FakeResponse(ok=True))
    monkeypatch.setattr(
        module,
        "extract_box_state_from_soup",
        lambda soup: (box["letters"], box["colors"], None),
    )

    response = client.get("/transfer_box", query_string={"hostname": "TestBox"})
    assert response.status_code == 405

    response = client.post(
        "/transfer_box",
        json={"hostname": "TestBox"},
        environ_overrides={"REMOTE_ADDR": "198.51.100.10"},
    )
    assert response.status_code == 200
    assert response.get_json() == {"status": "⏭️ Bereits aktuell"}


def test_remove_box_allows_remote_clients(webserver_app):
    module, client = webserver_app
    module.save_config({"boxen": {"TestBox": _empty_box(module)}, "boxOrder": ["TestBox"]})

    response = client.post(
        "/remove_box",
        json={"hostname": "TestBox"},
        environ_overrides={"REMOTE_ADDR": "198.51.100.10"},
    )
    assert response.status_code == 200
    assert response.get_json() == {"status": "removed"}

    config = module.load_config()
    assert "TestBox" not in config["boxen"]
    assert config["boxOrder"] == []


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


def test_update_box_rejects_malicious_color_payload(webserver_app):
    module, client = webserver_app
    module.save_config({"boxen": {"TestBox": _empty_box(module)}, "boxOrder": ["TestBox"]})

    malicious_color = "#123456\" onfocus=\"alert(1)\""
    response = client.post(
        "/update_box",
        json={
            "hostname": "TestBox",
            "day": "mo",
            "triggerIndex": 0,
            "color": malicious_color,
        },
    )

    assert response.status_code == 200
    assert response.get_json() == {"status": "success"}

    config = module.load_config()
    assert config["boxen"]["TestBox"]["colors"]["mo"][0] == module.DEFAULT_COLOR

    devices_response = client.get("/devices")
    assert devices_response.status_code == 200
    payload = devices_response.get_json()
    assert payload["boxen"]["TestBox"]["colors"]["mo"][0] == module.DEFAULT_COLOR
    assert malicious_color not in devices_response.get_data(as_text=True)


def test_update_box_sanitizes_letters_and_transfer(webserver_app, monkeypatch):
    module, client = webserver_app
    box = _empty_box(module, ip="1.2.3.4")
    module.save_config({"boxen": {"TestBox": box}, "boxOrder": ["TestBox"]})

    response = client.post(
        "/update_box",
        json={
            "hostname": "TestBox",
            "letters": {
                "mo": ["  a ", "€", None],
                "di": {"0": "b#", "1": "~", "2": "??"},
            },
        },
    )

    assert response.status_code == 200
    assert response.get_json() == {"status": "success"}

    config = module.load_config()
    letters = config["boxen"]["TestBox"]["letters"]
    assert letters["mo"][0] == "A"
    assert letters["mo"][1] == ""
    assert letters["mo"][2] == ""
    assert letters["di"][0] == "B"
    assert letters["di"][1] == "~"
    assert letters["di"][2] == "?"

    response = client.post(
        "/update_box",
        json={"hostname": "TestBox", "day": "mo", "triggerIndex": 2, "letter": " c "},
    )

    assert response.status_code == 200
    assert response.get_json() == {"status": "success"}

    config = module.load_config()
    assert config["boxen"]["TestBox"]["letters"]["mo"][2] == "C"

    response = client.post(
        "/update_box",
        json={"hostname": "TestBox", "day": "mo", "triggerIndex": 2, "letter": "€uro"},
    )

    assert response.status_code == 200
    assert response.get_json() == {"status": "success"}

    config = module.load_config()
    assert config["boxen"]["TestBox"]["letters"]["mo"][2] == ""

    remote_letters = {day: ["" for _ in range(module.TRIGGER_SLOTS)] for day in module.DAYS}
    remote_letters["mo"][0] = "Z"
    remote_letters["mo"][2] = "Z"
    remote_colors = {day: [module.DEFAULT_COLOR for _ in range(module.TRIGGER_SLOTS)] for day in module.DAYS}
    remote_delays = copy.deepcopy(module._default_delay_matrix())

    class FakeResponse:
        def __init__(self, text: str = "", ok: bool = True) -> None:
            self.text = text
            self.ok = ok

    def fake_get(url, *args, **kwargs):
        assert kwargs.get("allow_redirects") is False
        assert url == "http://1.2.3.4/"
        return FakeResponse("<html></html>", True)

    monkeypatch.setattr(module.requests, "get", fake_get)
    monkeypatch.setattr(
        module,
        "extract_box_state_from_soup",
        lambda soup: (
            copy.deepcopy(remote_letters),
            copy.deepcopy(remote_colors),
            copy.deepcopy(remote_delays),
        ),
    )
    monkeypatch.setattr(module, "fetch_trigger_delays", lambda ip: copy.deepcopy(remote_delays))

    captured = {}

    def fake_post(url, json=None, timeout=3, **kwargs):
        captured["url"] = url
        captured["json"] = json

        class PostResponse:
            ok = True

        return PostResponse()

    monkeypatch.setattr(module.requests, "post", fake_post)

    transfer_response = client.post("/transfer_box", json={"hostname": "TestBox"})

    assert transfer_response.status_code == 200
    assert transfer_response.get_json() == {"status": "✅ Übertragen"}
    assert captured["url"] == "http://1.2.3.4/updateAllLetters"
    payload = captured["json"]
    assert payload["letters"]["mo"][0] == "A"
    assert payload["letters"]["mo"][1] == ""
    assert payload["letters"]["mo"][2] == ""
    assert payload["letters"]["di"][0] == "B"
    assert payload["letters"]["di"][1] == "~"
    assert payload["letters"]["di"][2] == "?"


def test_devices_sanitizes_invalid_ip_addresses(webserver_app):
    module, client = webserver_app
    module.save_config({"boxen": {}, "boxOrder": []})

    malicious_ip = "192.0.2.99<script>alert(1)</script>"
    response = client.post(
        "/update_box",
        json={"hostname": "TestBox", "ip": malicious_ip},
    )

    assert response.status_code == 200
    assert response.get_json() == {"status": "success"}

    config = module.load_config()
    assert config["boxen"]["TestBox"]["ip"] == module.SAFE_IP_PLACEHOLDER

    devices_response = client.get("/devices")
    assert devices_response.status_code == 200
    devices_payload = devices_response.get_json()
    assert devices_payload["boxen"]["TestBox"]["ip"] == module.SAFE_IP_PLACEHOLDER
    assert module.SAFE_IP_PLACEHOLDER in devices_response.get_data(as_text=True)
    assert malicious_ip not in devices_response.get_data(as_text=True)


def test_devices_blocks_redirect_during_scan(webserver_app, monkeypatch, tmp_path):
    module, client = webserver_app

    module.LEASE_FILE = str(tmp_path / "dnsmasq.leases")
    Path(module.LEASE_FILE).write_text("0 00:11:22:33:44:55 1.2.3.4 hostname * *\n", encoding="utf-8")

    monkeypatch.setattr(module.subprocess, "call", lambda *args, **kwargs: 0)
    monkeypatch.setattr(module, "learn_box", lambda *args, **kwargs: pytest.fail("learn_box darf nicht aufgerufen werden"))

    class RedirectResponse:
        status_code = 302
        ok = True
        text = ""
        is_redirect = True
        is_permanent_redirect = False

    call_state = {"count": 0}

    def fake_get(url, *args, **kwargs):
        assert kwargs.get("allow_redirects") is False
        call_state["count"] += 1
        if url == "http://1.2.3.4/":
            return RedirectResponse()
        pytest.fail(f"Unerwarteter GET-Aufruf: {url}")

    monkeypatch.setattr(module.requests, "get", fake_get)

    response = client.get("/devices")

    assert call_state["count"] == 1
    assert response.status_code == 502
    assert response.get_json() == {
        "status": "❌ Unerwartete Weiterleitung",
        "details": "Geräte-Scan (1.2.3.4) lieferte HTTP 302 mit Weiterleitung",
    }


def test_devices_blocks_redirect_during_hostname_lookup(webserver_app, monkeypatch, tmp_path):
    module, client = webserver_app

    module.LEASE_FILE = str(tmp_path / "dnsmasq.leases")
    Path(module.LEASE_FILE).write_text("0 00:11:22:33:44:55 1.2.3.4 hostname * *\n", encoding="utf-8")

    monkeypatch.setattr(module.subprocess, "call", lambda *args, **kwargs: 0)
    monkeypatch.setattr(module, "learn_box", lambda *args, **kwargs: pytest.fail("learn_box darf nicht aufgerufen werden"))

    class FakeResponse:
        def __init__(self, text: str = "", status_code: int = 200, *, is_redirect: bool = False) -> None:
            self.text = text
            self.status_code = status_code
            self.ok = status_code == 200
            self.is_redirect = is_redirect
            self.is_permanent_redirect = False

    call_state = {"count": 0}

    def fake_get(url, *args, **kwargs):
        assert kwargs.get("allow_redirects") is False
        if url == "http://1.2.3.4/":
            call_state["count"] += 1
            if call_state["count"] == 1:
                return FakeResponse("<html><body><input name='hostname' value='BoxX'></body></html>")
            if call_state["count"] == 2:
                return FakeResponse(status_code=307, is_redirect=True)
        pytest.fail(f"Unerwarteter GET-Aufruf: {url}")

    monkeypatch.setattr(module.requests, "get", fake_get)

    response = client.get("/devices")

    assert call_state["count"] == 2
    assert response.status_code == 502
    assert response.get_json() == {
        "status": "❌ Unerwartete Weiterleitung",
        "details": "Hostname-Abfrage (1.2.3.4) lieferte HTTP 307 mit Weiterleitung",
    }


def test_devices_blocks_redirect_during_learn_box(webserver_app, monkeypatch, tmp_path):
    module, client = webserver_app

    module.LEASE_FILE = str(tmp_path / "dnsmasq.leases")
    Path(module.LEASE_FILE).write_text("0 00:11:22:33:44:55 1.2.3.4 hostname * *\n", encoding="utf-8")

    monkeypatch.setattr(module.subprocess, "call", lambda *args, **kwargs: 0)
    monkeypatch.setattr(module, "get_hostname_from_web", lambda ip: "BoxNeu")
    monkeypatch.setattr(module, "fetch_trigger_delays", lambda ip: pytest.fail("Trigger-Delays dürfen nicht abgefragt werden"))

    class FakeResponse:
        def __init__(self, text: str = "", status_code: int = 200, *, is_redirect: bool = False) -> None:
            self.text = text
            self.status_code = status_code
            self.ok = status_code == 200
            self.is_redirect = is_redirect
            self.is_permanent_redirect = False

    call_state = {"count": 0}

    def fake_get(url, *args, **kwargs):
        assert kwargs.get("allow_redirects") is False
        if url == "http://1.2.3.4/":
            call_state["count"] += 1
            if call_state["count"] == 1:
                return FakeResponse("<html><body><input name='hostname' value='BoxNeu'></body></html>")
            if call_state["count"] == 2:
                return FakeResponse(status_code=302, is_redirect=True)
        pytest.fail(f"Unerwarteter GET-Aufruf: {url}")

    monkeypatch.setattr(module.requests, "get", fake_get)

    response = client.get("/devices")

    assert call_state["count"] == 2
    assert response.status_code == 502
    assert response.get_json() == {
        "status": "❌ Unerwartete Weiterleitung",
        "details": "Box-Lernvorgang (1.2.3.4) lieferte HTTP 302 mit Weiterleitung",
    }


def test_update_box_allows_remote_clients(webserver_app):
    module, client = webserver_app
    module.save_config({"boxen": {"TestBox": _empty_box(module)}, "boxOrder": []})

    response = client.post(
        "/update_box",
        json={"hostname": "TestBox", "day": "mo", "triggerIndex": 0, "letter": "Z"},
        environ_overrides={"REMOTE_ADDR": "198.51.100.10"},
    )

    assert response.status_code == 200
    assert response.get_json() == {"status": "success"}
    config = module.load_config()
    assert config["boxen"]["TestBox"]["letters"]["mo"][0] == "Z"


def test_delay_inputs_are_clamped_to_upper_bound(webserver_app, monkeypatch):
    module, client = webserver_app
    box = _empty_box(module)
    box["ip"] = "1.2.3.4"
    module.save_config({"boxen": {"TestBox": box}, "boxOrder": []})

    response = client.post(
        "/update_box",
        json={"hostname": "TestBox", "day": "mo", "triggerIndex": 0, "delay": 1500},
    )

    assert response.status_code == 200

    config = module.load_config()
    assert config["boxen"]["TestBox"]["delays"]["mo"][0] == 999

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
        assert kwargs.get("allow_redirects") is False
        if url.endswith("/api/trigger-delays"):
            return FakeResponse(
                ok=True,
                json_data={"delays": {day: [module.DEFAULT_DELAY for _ in range(module.TRIGGER_SLOTS)] for day in module.DAYS}},
            )
        return FakeResponse("<html></html>", True)

    monkeypatch.setattr(module.requests, "get", fake_get)

    captured = {}

    def fake_post(url, json=None, timeout=3, **kwargs):
        captured["url"] = url
        captured["json"] = json

        class PostResponse:
            ok = True

        return PostResponse()

    monkeypatch.setattr(module.requests, "post", fake_post)

    transfer_response = client.post("/transfer_box", json={"hostname": "TestBox"})
    assert transfer_response.status_code == 200
    assert transfer_response.get_json() == {"status": "✅ Übertragen"}
    assert "json" in captured
    assert captured["json"]["delays"]["mo"][0] == 999


def test_update_box_order_allows_remote_clients(webserver_app):
    module, client = webserver_app
    module.save_config(
        {
            "boxen": {
                "BoxA": _empty_box(module, ip="10.0.0.1"),
                "BoxB": _empty_box(module, ip="10.0.0.2"),
            },
            "boxOrder": ["BoxA", "BoxB"],
        }
    )

    response = client.post(
        "/update_box_order",
        json={"boxOrder": ["BoxB", "BoxA"]},
        environ_overrides={"REMOTE_ADDR": "198.51.100.10"},
    )

    assert response.status_code == 200
    assert response.get_json() == {"status": "success"}
    config = module.load_config()
    assert config["boxOrder"] == ["BoxB", "BoxA"]


def test_update_box_order_accepts_empty_list(webserver_app):
    module, client = webserver_app
    module.save_config(
        {
            "boxen": {
                "BoxA": _empty_box(module, ip="10.0.0.1"),
                "BoxB": _empty_box(module, ip="10.0.0.2"),
            },
            "boxOrder": ["BoxA", "BoxB"],
        }
    )

    response = client.post("/update_box_order", json={"boxOrder": []})

    assert response.status_code == 200
    assert response.get_json() == {"status": "success"}
    config = module.load_config()
    assert config["boxOrder"] == []


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
        assert kwargs.get("allow_redirects") is False
        if url.endswith("/api/trigger-delays"):
            return FakeResponse(ok=True, json_data={"delays": {day: [0 for _ in range(module.TRIGGER_SLOTS)] for day in module.DAYS}})
        return FakeResponse(fake_html, True)

    monkeypatch.setattr(module.requests, "get", fake_get)

    captured = {}

    def fake_post(url, json=None, timeout=3, **kwargs):
        captured["url"] = url
        captured["json"] = json

        class PostResponse:
            ok = True

        return PostResponse()

    monkeypatch.setattr(module.requests, "post", fake_post)

    response = client.post("/transfer_box", json={"hostname": "TestBox"})
    assert response.status_code == 200
    assert response.get_json() == {"status": "✅ Übertragen"}
    assert captured["url"].endswith("/updateAllLetters")
    expected_letters = {
        day: [module.sanitize_letter(letter) for letter in letters[day]]
        for day in module.DAYS
    }
    assert captured["json"] == {"letters": expected_letters, "colors": colors, "delays": delays}


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
        assert kwargs.get("allow_redirects") is False
        if url.endswith("/api/trigger-delays"):
            return FakeResponse(ok=True, json_data={"delays": {day: [0 for _ in range(module.TRIGGER_SLOTS)] for day in module.DAYS}})
        return FakeResponse("<html></html>", True)

    monkeypatch.setattr(module.requests, "get", fake_get)

    captured = {}

    def fake_post(url, json=None, timeout=3, **kwargs):
        captured["url"] = url
        captured["json"] = json

        class PostResponse:
            ok = True

        return PostResponse()

    monkeypatch.setattr(module.requests, "post", fake_post)

    response = client.post("/transfer_box", json={"hostname": "TestBox"})
    assert response.status_code == 200
    assert response.get_json() == {"status": "✅ Übertragen"}
    assert "json" in captured

    expected_delays = {
        day: [module._coerce_delay_value(value) for value in decimal_template] for day in module.DAYS
    }

    assert captured["json"]["delays"] == expected_delays
    for day in module.DAYS:
        assert all(isinstance(value, int) for value in captured["json"]["delays"][day])


def test_reload_all_allows_remote_clients(webserver_app, monkeypatch):
    module, client = webserver_app
    module.save_config({"boxen": {"AltBox": {"ip": "1.2.3.4"}}, "boxOrder": ["AltBox"]})

    called = {"count": 0}

    def fake_get_connected_devices():
        called["count"] += 1
        return []

    monkeypatch.setattr(module, "get_connected_devices", fake_get_connected_devices)

    response = client.post(
        "/reload_all",
        environ_base={"REMOTE_ADDR": "198.51.100.10"},
    )

    assert response.status_code == 200
    assert response.get_json() == {"status": "reloaded"}
    assert called["count"] == 1

    config = module.load_config()
    assert config["boxen"] == {}
    assert config["boxOrder"] == []


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
        assert kwargs.get("allow_redirects") is False
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

    response = client.post("/transfer_box", json={"hostname": "TestBox"})
    assert response.status_code == 200
    assert response.get_json() == {"status": "⏭️ Bereits aktuell"}
    assert post_calls == []


def test_transfer_box_ignores_remote_color_case_differences(webserver_app, monkeypatch):
    module, client = webserver_app

    box = _empty_box(module)
    box["letters"]["mo"][0] = "A"
    box["colors"]["mo"][0] = "#ff00ff"

    module.save_config({"boxen": {"TestBox": box}, "boxOrder": []})

    remote_html = (
        "<html><body>"
        "<select name='letter_0_1'><option value='A' selected>A</option></select>"
        "<input type='color' name='color_0_1' value='#FF00FF'>"
        "</body></html>"
    )

    delays_payload = {"delays": {day: list(box["delays"][day]) for day in module.DAYS}}

    class FakeResponse:
        def __init__(self, text: str = "", ok: bool = True, json_data=None, status_code: int = 200) -> None:
            self.text = text
            self.ok = ok
            self._json = json_data
            self.status_code = status_code
            self.is_redirect = False
            self.is_permanent_redirect = False

        def json(self):
            if self._json is None:
                raise ValueError("No JSON data")
            return self._json

    def fake_get(url, *args, **kwargs):
        assert kwargs.get("allow_redirects") is False
        if url.endswith("/api/trigger-delays"):
            return FakeResponse(ok=True, json_data=delays_payload)
        assert url == f"http://{box['ip']}/"
        return FakeResponse(remote_html, True)

    post_calls = []

    def fake_post(*args, **kwargs):
        post_calls.append((args, kwargs))
        return FakeResponse()

    monkeypatch.setattr(module.requests, "get", fake_get)
    monkeypatch.setattr(module.requests, "post", fake_post)

    response = client.post("/transfer_box", json={"hostname": "TestBox"})

    assert response.status_code == 200
    assert response.get_json() == {"status": "⏭️ Bereits aktuell"}
    assert post_calls == []


def test_extract_box_state_supports_trigger_first_schema(webserver_app):
    module, _client = webserver_app

    expected_letters = {day: ["" for _ in range(module.TRIGGER_SLOTS)] for day in module.DAYS}
    expected_colors = {
        day: [module.DEFAULT_COLOR for _ in range(module.TRIGGER_SLOTS)] for day in module.DAYS
    }
    expected_delays = {
        day: [module.DEFAULT_DELAY for _ in range(module.TRIGGER_SLOTS)] for day in module.DAYS
    }

    html_parts = ["<html><body>"]
    for day_index, day in enumerate(module.DAYS):
        for slot in range(module.TRIGGER_SLOTS):
            letter_value = f"L{day_index}{slot}"
            color_value = f"#{slot}{day_index}{slot}{day_index}{slot}{day_index}"
            expected_letters[day][slot] = module.sanitize_letter(letter_value)
            expected_colors[day][slot] = color_value

            raw_delay = f"{day_index * 10 + slot + 0.4}"
            expected_delays[day][slot] = module._coerce_delay_value(raw_delay)

            if (day_index + slot) % 2 == 0:
                select_name = f"letter_{slot}_{day_index}"
                color_name = f"color_{slot}_{day_index}"
            else:
                select_name = f"letter{slot}{day_index}"
                color_name = f"color{slot}{day_index}"

            if slot == 0:
                delay_name = f"delay_{day_index}"
            elif (day_index + slot) % 3 == 0:
                delay_name = f"delay_{slot}_{day_index}"
            elif (day_index + slot) % 3 == 1:
                delay_name = f"delay_{day_index}_{slot}"
            else:
                delay_name = f"delay{slot}{day_index}"

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
            html_parts.append(
                f"<input type='number' name='{delay_name}' value='{raw_delay}'>"
            )
    html_parts.append("</body></html>")

    soup = module.BeautifulSoup("".join(html_parts), "html.parser")
    letters, colors, delays = module.extract_box_state_from_soup(soup)

    assert letters == expected_letters
    assert colors == expected_colors
    assert delays == expected_delays


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
        assert kwargs.get("allow_redirects") is False
        return FakeResponse()

    monkeypatch.setattr(module.requests, "get", fake_get)

    delays = module.fetch_trigger_delays("1.2.3.4")
    assert delays is not None

    assert delays["mo"] == [module._coerce_delay_value(value) for value in payload["delays"]["mo"]]
    assert delays["di"] == [module._coerce_delay_value(payload["delays"]["di"].get(str(idx))) for idx in range(module.TRIGGER_SLOTS)]
    assert delays["mi"] == [module.DEFAULT_DELAY for _ in range(module.TRIGGER_SLOTS)]
    assert delays["so"] == [module._coerce_delay_value(value) for value in payload["delays"]["so"]]


def test_learn_box_uses_html_delays_when_api_unavailable(webserver_app, monkeypatch):
    module, _ = webserver_app

    expected_delays = {
        day: [module.DEFAULT_DELAY for _ in range(module.TRIGGER_SLOTS)] for day in module.DAYS
    }

    html_parts = ["<html><body>"]
    for day_index, day in enumerate(module.DAYS):
        for slot in range(module.TRIGGER_SLOTS):
            raw_delay = f"{day_index * 5 + slot + 0.6}"
            expected_delays[day][slot] = module._coerce_delay_value(raw_delay)

            if slot == 0:
                delay_name = f"delay_{day_index}"
            elif slot == 1:
                delay_name = f"delay_{day_index}_{slot}"
            else:
                delay_name = f"delay_{slot}_{day_index}"

            html_parts.append(
                f"<select name='letter_{slot}_{day_index}'><option value='X{day_index}{slot}' selected></option></select>"
            )
            html_parts.append(
                f"<input name='color_{slot}_{day_index}' value='{module.DEFAULT_COLOR}'>"
            )
            html_parts.append(
                f"<input type='number' name='{delay_name}' value='{raw_delay}'>"
            )
    html_parts.append("</body></html>")
    fake_html = "".join(html_parts)

    class FakeResponse:
        def __init__(self, text: str, ok: bool = True) -> None:
            self.text = text
            self.ok = ok

    def fake_get(url, *args, **kwargs):
        if url.endswith("/api/trigger-delays"):
            raise module.requests.RequestException("api down")
        assert url == "http://1.2.3.4/"
        assert kwargs.get("allow_redirects") is False
        return FakeResponse(fake_html, True)

    monkeypatch.setattr(module.requests, "get", fake_get)

    module.learn_box("1.2.3.4", "HtmlBox")

    config = module.load_config()
    assert "HtmlBox" in config["boxen"]
    learned_delays = config["boxen"]["HtmlBox"]["delays"]

    assert learned_delays == expected_delays
    assert learned_delays != module._default_delay_matrix()

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


def test_shutdown_rejects_remote_clients_without_token(webserver_app, monkeypatch, caplog):
    module, client = webserver_app
    monkeypatch.setattr(module.os, "system", lambda cmd: pytest.fail("poweroff darf ohne Token nicht ausgeführt werden"))
    monkeypatch.setenv(module.SHUTDOWN_TOKEN_ENV_VAR, "SehrSicheresToken")
    caplog.set_level(logging.WARNING)

    response = client.post("/shutdown", environ_overrides={"REMOTE_ADDR": "203.0.113.5"})

    assert response.status_code == 403
    assert response.get_json() == {"status": "❌ Zugriff verweigert", "details": "Authentifizierungs-Token fehlt"}
    assert any("Shutdown-Anfrage abgelehnt" in record.message for record in caplog.records)


def test_shutdown_allows_remote_clients_with_valid_token(webserver_app, monkeypatch):
    module, client = webserver_app
    calls = []

    def fake_system(cmd: str) -> None:
        calls.append(cmd)

    monkeypatch.setattr(module.os, "system", fake_system)
    monkeypatch.setenv(module.SHUTDOWN_TOKEN_ENV_VAR, "SehrSicheresToken")

    response = client.post(
        "/shutdown",
        environ_overrides={"REMOTE_ADDR": "203.0.113.5"},
        headers={module.SHUTDOWN_TOKEN_HEADER: "SehrSicheresToken"},
    )

    assert response.status_code == 200
    assert response.get_json() == {"status": "OK"}
    assert calls == ["poweroff"]


def test_shutdown_allows_loopback_without_auth(webserver_app, monkeypatch):
    module, client = webserver_app
    monkeypatch.setattr(module.os, "system", lambda cmd: None)

    response = client.post("/shutdown", environ_overrides={"REMOTE_ADDR": "127.0.0.1"})
    assert response.status_code == 200
    assert response.get_json() == {"status": "OK"}
