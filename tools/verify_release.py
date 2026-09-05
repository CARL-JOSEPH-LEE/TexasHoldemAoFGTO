"""Launch the actual package with only system tools on PATH and test its engines."""

import argparse
import hashlib
import json
import os
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    exe, work = args.exe.resolve(), args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    manifest = exe.parent / "release-manifest.json"
    if manifest.is_file():
        doc = json.loads(manifest.read_text(encoding="utf-8"))
        for name, expected in doc["files"].items():
            file = (exe.parent / name).resolve()
            if not file.is_relative_to(exe.parent):
                raise ValueError("invalid manifest path")
            with file.open("rb") as stream:
                if hashlib.file_digest(stream, "sha256").hexdigest() != expected:
                    raise ValueError(f"release integrity mismatch: {name}")
    report = work / "portable-report.json"
    report.unlink(missing_ok=True)
    environment = os.environ.copy()
    environment.update(
        {
            "AOF2_GUI_SELF_TEST_REPORT": str(report),
            "AOF_STRATEGY_DIR": str(work / "用户 策略"),
            "AOF2_GUI_TEST_REQUIRE_DATA": "1",
            "QT_QPA_PLATFORM": "windows" if os.name == "nt" else "offscreen",
            "PYTHONPATH": "",
            "PYTHONHOME": "",
        }
    )
    if os.name == "nt":
        system = Path(environment["SYSTEMROOT"])
        environment["PATH"] = os.pathsep.join(map(str, (system / "System32", system)))
    environment.pop("AOF2_GUI_TEST_EXIT_MS", None)
    result = subprocess.run(
        [str(exe)],
        cwd=work,
        env=environment,
        capture_output=True,
        timeout=75,
        creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
    )
    if result.returncode or not report.is_file():
        raise RuntimeError(
            f"portable process failed: {result.returncode} {result.stdout!r} {result.stderr!r}"
        )
    data = json.loads(report.read_text(encoding="utf-8"))
    assert data["ok"] and data["focused_audit"] and data["modes"] == [2, 3, 4], data
    print(
        json.dumps(
            {
                "ok": True,
                "modes": data["modes"],
                "focused_audit": data["focused_audit"],
                "job_history": data["job_history"],
                "report": str(report),
            },
            ensure_ascii=False,
        )
    )


if __name__ == "__main__":
    main()
