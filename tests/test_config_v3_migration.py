from __future__ import annotations

import subprocess
from pathlib import Path


def _build_test_binary(tmp_path: Path) -> Path:
    build_dir = tmp_path / "build"
    build_dir.mkdir()

    binary = build_dir / "config_v3_migration"
    sources = [
        "tests/config_v3_migration_harness.cpp",
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


def test_load_config_migrates_version3_layout(tmp_path) -> None:
    binary = _build_test_binary(Path(tmp_path))
    subprocess.run([str(binary)], check=True, cwd=Path.cwd())
