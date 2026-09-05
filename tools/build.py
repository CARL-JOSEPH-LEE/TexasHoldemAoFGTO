"""Build the native engine without relying on a developer-specific generator."""

import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def build():
    configure = ["cmake", "-S", str(ROOT), "-B", str(ROOT / "build"), "-DCMAKE_BUILD_TYPE=Release"]
    if not (ROOT / "build" / "CMakeCache.txt").exists() and shutil.which("ninja"):
        if shutil.which("g++") or shutil.which("cl") or shutil.which("clang++"):
            configure += ["-G", "Ninja"]
    subprocess.run(configure, check=True)
    subprocess.run(
        ["cmake", "--build", str(ROOT / "build"), "--config", "Release", "--parallel", "8"], check=True
    )
    subprocess.run(
        ["ctest", "--test-dir", str(ROOT / "build"), "-C", "Release", "--output-on-failure"], check=True
    )


if __name__ == "__main__":
    build()
