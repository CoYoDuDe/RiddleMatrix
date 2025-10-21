from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

import pytest


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


@pytest.fixture
def webserver_app(tmp_path):
    module = _load_webserver(tmp_path)
    with module.app.test_client() as client:
        yield module, client


def test_transfer_box_returns_json_on_get_error(webserver_app, monkeypatch):
    module, client = webserver_app
    module.save_config({"boxen": {"TestBox": {"ip": "1.2.3.4"}}, "boxOrder": []})

    def raise_request(*args, **kwargs):
        raise module.requests.RequestException("boom")

    monkeypatch.setattr(module.requests, "get", raise_request)

    response = client.get("/transfer_box", query_string={"hostname": "TestBox"})
    assert response.status_code == 200
    assert response.get_json() == {"status": "❌ Box nicht erreichbar"}


def test_transfer_box_returns_json_on_post_error(webserver_app, monkeypatch):
    module, client = webserver_app
    module.save_config({"boxen": {"TestBox": {"ip": "1.2.3.4", "mo": "A"}}, "boxOrder": []})

    class FakeResponse:
        def __init__(self, text: str, ok: bool = True) -> None:
            self.text = text
            self.ok = ok

    monkeypatch.setattr(module.requests, "get", lambda *_, **__: FakeResponse("", True))

    def raise_post(*args, **kwargs):
        raise module.requests.RequestException("boom")

    monkeypatch.setattr(module.requests, "post", raise_post)

    response = client.get("/transfer_box", query_string={"hostname": "TestBox"})
    assert response.status_code == 200
    assert response.get_json() == {"status": "❌ Fehler bei Übertragung"}
