from __future__ import annotations

import os
import subprocess
from pathlib import Path

HOOK_PATH = Path(__file__).resolve().parents[1] / "USBStick-Setup" / "hooks.d" / "10-provision-webserver.sh"


def run_hook(target_root: Path | str, *, dry_run: bool = True) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    env["TARGET_ROOT"] = str(target_root)
    if dry_run:
        env["PROVISION_DRY_RUN"] = "1"
    return subprocess.run(
        ["bash", str(HOOK_PATH), str(target_root)],
        cwd=HOOK_PATH.parent,
        check=False,
        text=True,
        capture_output=True,
        env=env,
    )


def test_dry_run_on_host_root():
    result = run_hook("/")
    assert result.returncode == 0, result.stderr
    assert "Dry-run aktiviert" in result.stdout
    assert "virtuelle Umgebung auf dem Host" in result.stdout
    assert "Python-Abhängigkeiten im virtuellen Host-Environment" in result.stdout
    assert "Provisioning hook completed" in result.stdout


def test_dry_run_on_offline_target(tmp_path):
    target_root = tmp_path / "offline-root"
    (target_root / "usr/bin").mkdir(parents=True)
    python_stub = target_root / "usr/bin/python3"
    python_stub.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
    python_stub.chmod(0o755)

    result = run_hook(target_root)
    assert result.returncode == 0, result.stderr
    assert "Dry-run aktiviert" in result.stdout
    assert "virtuelle Umgebung via chroot" in result.stdout
    assert "Python-Abhängigkeiten im Zielsystem via chroot" in result.stdout
    assert "Provisioning hook completed" in result.stdout
