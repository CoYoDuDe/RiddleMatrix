from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

import pytest


def _build_letters_binary(tmp_path: Path) -> Path:
    build_dir = tmp_path / "build"
    build_dir.mkdir()

    binary = build_dir / "letters_random_symbol"
    sources = [
        "tests/letters_random_symbol_harness.cpp",
        "src/letters.cpp",
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


def test_star_is_random_selector_not_bitmap(tmp_path) -> None:
    if shutil.which("g++") is None:
        pytest.skip("g++ is required for the host-side letters harness")

    binary = _build_letters_binary(Path(tmp_path))
    subprocess.run([str(binary)], check=True, cwd=Path.cwd())
