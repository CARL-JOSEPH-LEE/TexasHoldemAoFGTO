"""Fetch pinned upstream sources and retain their license/attribution files."""

import hashlib
import json
import re
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION = "6.11.2"
SOURCES = ROOT / "build" / "dependency_sources"
LICENSES = ROOT / "third_party" / "licenses"


def main():
    SOURCES.mkdir(parents=True, exist_ok=True)
    LICENSES.mkdir(parents=True, exist_ok=True)
    urls = [
        f"https://download.qt.io/official_releases/qt/6.11/{VERSION}/submodules/{module}-everywhere-src-{VERSION}.tar.xz"
        for module in ("qtbase", "qtsvg", "qtimageformats")
    ]
    urls.append(
        f"https://download.qt.io/official_releases/QtForPython/pyside6/PySide6-{VERSION}-src/pyside-setup-everywhere-src-{VERSION}.tar.xz"
    )
    manifest = []
    manifest_file = ROOT / "third_party/sources.json"
    pinned = (
        {item["filename"]: item["sha256"] for item in json.loads(manifest_file.read_text(encoding="utf-8"))}
        if manifest_file.is_file()
        else {}
    )
    for url in urls:
        path = SOURCES / url.rsplit("/", 1)[-1]
        if not path.is_file():
            temp = path.with_suffix(".download")
            # System curl uses the platform trust store, including corporate CAs.
            # Never disable TLS validation when following Qt's official mirrors.
            subprocess.run(
                [
                    "curl.exe" if sys.platform == "win32" else "curl",
                    "--fail",
                    "--location",
                    "--silent",
                    "--show-error",
                    "--retry",
                    "2",
                    "--connect-timeout",
                    "20",
                    "--max-time",
                    "300",
                    "--output",
                    str(temp),
                    url,
                ],
                check=True,
            )
            temp.replace(path)
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if path.name in pinned and digest != pinned[path.name]:
            raise ValueError(f"Pinned source checksum mismatch: {path.name}")
        manifest.append({"filename": path.name, "url": url, "sha256": digest, "bytes": path.stat().st_size})
        print(path.name, digest, flush=True)
        module = path.name.split("-everywhere")[0]
        with tarfile.open(path, "r:xz") as archive:
            for member in archive:
                relative = Path(*Path(member.name).parts[1:])
                if (
                    not member.isfile()
                    or member.size > 1_000_000
                    or ".." in relative.parts
                    or relative.is_absolute()
                ):
                    continue
                if (
                    "LICENSES" in relative.parts
                    or re.match(r"(licen[cs]e|copying|copyright|notice)([._-].*)?$", relative.name, re.I)
                    or relative.name == "qt_attribution.json"
                ):
                    target = LICENSES / module / relative
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(archive.extractfile(member).read())
    (ROOT / "third_party" / "sources.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    shutil.copy2(Path(sys.prefix) / "LICENSE.txt", LICENSES / "Python-3.12-LICENSE.txt")
    runtime_licenses = {
        "GCC-COPYING3.txt": "https://raw.githubusercontent.com/gcc-mirror/gcc/releases/gcc-16.1.0/COPYING3",
        "GCC-Runtime-Exception.txt": "https://raw.githubusercontent.com/gcc-mirror/gcc/releases/gcc-16.1.0/COPYING.RUNTIME",
        "MinGW-w64-COPYING.txt": "https://raw.githubusercontent.com/mingw-w64/mingw-w64/master/COPYING",
        "winpthreads-COPYING.txt": "https://raw.githubusercontent.com/mingw-w64/mingw-w64/master/mingw-w64-libraries/winpthreads/COPYING",
    }
    for name, url in runtime_licenses.items():
        destination = LICENSES / name
        if not destination.is_file():
            temp = destination.with_suffix(".download")
            subprocess.run(
                [
                    "curl.exe" if sys.platform == "win32" else "curl",
                    "--fail",
                    "--location",
                    "--silent",
                    "--show-error",
                    "--max-time",
                    "30",
                    "--output",
                    str(temp),
                    url,
                ],
                check=True,
            )
            temp.replace(destination)


if __name__ == "__main__":
    main()
