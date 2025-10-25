from __future__ import annotations

import subprocess
from pathlib import Path


def _build_letters_binary(tmp_path: Path) -> Path:
    build_dir = tmp_path / "build"
    build_dir.mkdir()

    binary = build_dir / "letters_sunplusrad"
    sources = [
        "tests/letters_sunplusrad_harness.cpp",
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


def test_letters_include_sunplusrad(tmp_path) -> None:
    binary = _build_letters_binary(Path(tmp_path))
    subprocess.run([str(binary)], check=True, cwd=Path.cwd())
