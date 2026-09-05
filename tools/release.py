"""Build a traceable desktop release with source, licenses and file hashes."""

import argparse
import hashlib
import importlib.metadata
import json
import os
import platform
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "gui"))
from workspace import VERSION  # noqa: E402


def digest(path):
    with path.open("rb") as file:
        return hashlib.file_digest(file, "sha256").hexdigest()


def source_archive(destination):
    includes = ["src", "gui", "tests", "tools", "OMPEval", "docs", "third_party", ".github", "assets"]
    files = set()
    for directory in includes:
        files.update(
            p
            for p in (ROOT / directory).rglob("*")
            if p.is_file()
            and not any(part in (".git", "__pycache__") for part in p.parts)
            and p.suffix not in (".pyc", ".exe", ".a", ".o", ".dll")
        )
    for pattern in (
        "requirements*.txt",
        "*.bat",
        "CMakeLists.txt",
        "LICENSE",
        "README.md",
        "*.toml",
        "pytest.ini",
        "THIRD_PARTY_NOTICES.txt",
        "data/strategy*.bin",
        "data/PROVENANCE*.txt",
    ):
        files.update(ROOT.glob(pattern))
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(files):
            archive.write(path, Path("AoFStudio-source") / path.relative_to(ROOT))


def license_directory():
    destination = ROOT / "build" / "release-licenses"
    destination.mkdir(parents=True, exist_ok=True)
    if not (ROOT / "third_party/licenses/qtbase/LICENSES/LGPL-3.0-only.txt").is_file():
        raise SystemExit("Run python tools/prepare_sources.py before packaging.")
    shutil.copytree(ROOT / "third_party/licenses", destination / "upstream", dirs_exist_ok=True)
    for source, name in (
        (ROOT / "LICENSE", "Application-MIT.txt"),
        (ROOT / "OMPEval/LICENSE.txt", "OMPEval-ISC.txt"),
        (ROOT / "OMPEval/LICENSE-libdivide.txt", "libdivide.txt"),
        (ROOT / "THIRD_PARTY_NOTICES.txt", "THIRD_PARTY_NOTICES.txt"),
    ):
        shutil.copy2(source, destination / name)
    for name in ("pyinstaller", "PySide6-Essentials", "shiboken6"):
        distribution = importlib.metadata.distribution(name)
        for member in distribution.files or []:
            if any(word in member.name.lower() for word in ("license", "copying")):
                source = Path(distribution.locate_file(member))
                if source.is_file():
                    target = destination / name / member.name
                    target.parent.mkdir(exist_ok=True)
                    shutil.copy2(source, target)
    return destination


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--format", choices=("onedir", "onefile"), default="onedir")
    args = parser.parse_args()
    if not args.skip_build:
        subprocess.run([sys.executable, str(ROOT / "tools/build.py")], check=True)
    binaries = [
        ROOT / "build" / (name + (".exe" if os.name == "nt" else "")) for name in ("aof2_train", "aof2_eval")
    ]
    for binary in binaries:
        result = json.loads(subprocess.check_output([str(binary), "--version"]))
        if result["version"] != VERSION:
            raise SystemExit("GUI/native engine version mismatch")
    licenses = license_directory()
    strategies = sorted((ROOT / "data").glob("strategy*.bin"))
    manifest_path = ROOT / "build/bundle-manifest.json"
    manifest_path.write_text(json.dumps({p.name: digest(p) for p in strategies}, indent=2), encoding="utf-8")
    command = [
        sys.executable,
        "-m",
        "PyInstaller",
        "--noconfirm",
        "--clean",
        f"--{args.format}",
        "--windowed",
        "--name",
        "TexasHoldemAoFGTO",
        "--paths",
        str(ROOT / "gui"),
        "--distpath",
        str(ROOT / "dist"),
        "--workpath",
        str(ROOT / "build/pyinstaller" / args.format),
        "--specpath",
        str(ROOT / "build"),
        "--exclude-module",
        "PyQt6",
        "--exclude-module",
        "PyQt5",
        "--exclude-module",
        "PySide2",
    ]
    for binary in binaries:
        command.extend(["--add-binary", f"{binary}{os.pathsep}bin"])
    for path in strategies:
        command.extend(["--add-data", f"{path}{os.pathsep}data"])
    for path, target in ((licenses, "licenses"), (manifest_path, "data"), (ROOT / "docs", "docs")):
        command.extend(["--add-data", f"{path}{os.pathsep}{target}"])
    command.append(str(ROOT / "gui/aof2_gui.py"))
    command[-1:-1] = ["--add-data", f"{ROOT / 'assets'}{os.pathsep}assets"]
    if os.name == "nt":
        subprocess.run([sys.executable, str(ROOT / "tools/create_icon.py")], check=True)
        command[-1:-1] = ["--icon", str(ROOT / "build/app.ico")]
        version_file = ROOT / "build/windows-version.txt"
        version_tuple = tuple(map(int, VERSION.split("."))) + (0,)
        version_file.write_text(
            f"VSVersionInfo(ffi=FixedFileInfo(filevers={version_tuple!r}, prodvers={version_tuple!r}, "
            "mask=0x3f, flags=0, OS=0x40004, fileType=1, subtype=0, date=(0,0)), "
            "kids=[StringFileInfo([StringTable('040904B0', ["
            "StringStruct('CompanyName', 'CARL JOSEPH LEE'), "
            "StringStruct('FileDescription', 'Texas Holdem AoF Studio'), "
            f"StringStruct('FileVersion', '{VERSION}'), StringStruct('ProductVersion', '{VERSION}'), "
            "StringStruct('ProductName', 'Texas Holdem AoF Studio'), "
            "StringStruct('LegalCopyright', 'Copyright 2026 CARL JOSEPH LEE')])]), "
            "VarFileInfo([VarStruct('Translation', [1033, 1200])])])",
            encoding="utf-8",
        )
        command[-1:-1] = ["--version-file", str(version_file)]
    environment = os.environ.copy()
    if os.name == "nt":
        system = Path(os.environ["SYSTEMROOT"])
        environment["PATH"] = os.pathsep.join(
            str(p) for p in (system / "System32", system, Path(sys.executable).parent)
        )
    subprocess.run(command, cwd=ROOT, env=environment, check=True)
    dependencies = [
        {"name": name, "version": importlib.metadata.version(name)}
        for name in (
            "PySide6-Essentials",
            "shiboken6",
            "pyinstaller",
            "pyinstaller-hooks-contrib",
            "packaging",
        )
    ]
    release = {
        "schema": 1,
        "version": VERSION,
        "format": args.format,
        "python": platform.python_version(),
        "platform": platform.platform(),
        "dependencies": dependencies,
        "engines": {binary.name: digest(binary) for binary in binaries},
        "strategy_files": {p.name: digest(p) for p in strategies},
    }
    if args.format == "onedir":
        folder = ROOT / "dist/TexasHoldemAoFGTO"
        for filename in ("THIRD_PARTY_NOTICES.txt", "README.md"):
            shutil.copy2(ROOT / filename, folder / filename)
        shutil.copytree(ROOT / "docs", folder / "docs", dirs_exist_ok=True)
        source_archive(folder / "AoFStudio-source.zip")
        shutil.copytree(ROOT / "build/dependency_sources", folder / "dependency-sources", dirs_exist_ok=True)
        shutil.copy2(ROOT / "third_party/sources.json", folder / "dependency-sources/sources.json")
        release["files"] = {
            str(p.relative_to(folder)).replace("\\", "/"): digest(p)
            for p in sorted(folder.rglob("*"))
            if p.is_file() and p.name != "release-manifest.json"
        }
        (folder / "release-manifest.json").write_text(
            json.dumps(release, ensure_ascii=False, indent=2), encoding="utf-8"
        )
        target = ROOT / "dist" / f"AoFStudio-{VERSION}-windows-x64"
        shutil.make_archive(str(target), "zip", folder.parent, folder.name)
        print(f"Folder release: {target}.zip")
    else:
        executable = ROOT / "dist/TexasHoldemAoFGTO.exe"
        release["sha256"] = digest(executable)
        executable.with_suffix(".manifest.json").write_text(
            json.dumps(release, ensure_ascii=False, indent=2), encoding="utf-8"
        )
        print(f"Single executable: {executable}")


if __name__ == "__main__":
    main()
