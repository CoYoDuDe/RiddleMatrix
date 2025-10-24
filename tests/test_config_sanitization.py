from __future__ import annotations

import subprocess
from pathlib import Path


def _build_test_binary(tmp_path: Path) -> Path:
    build_dir = tmp_path / "build"
    build_dir.mkdir()

    binary = build_dir / "config_sanitization"
    sources = [
        "tests/config_sanitization_harness.cpp",
        "src/config.cpp",
    ]

    command = [
        "g++",
        "-std=c++17",
        "-DRIDDLEMATRIX_HOST_TEST",
        "-Itests/stubs",
        "-Isrc",
        "-o",
        str(binary),
    ] + sources

    subprocess.run(command, check=True, cwd=Path.cwd())
    return binary


def test_load_config_recovers_from_ff(tmp_path) -> None:
    binary = _build_test_binary(Path(tmp_path))
    subprocess.run([str(binary)], check=True, cwd=Path.cwd())
