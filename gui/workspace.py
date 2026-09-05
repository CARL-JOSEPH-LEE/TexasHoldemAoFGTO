"""Local training journal. No network, telemetry, or pickle deserialization."""

from __future__ import annotations

import hashlib
import json
import os
import sqlite3
import uuid
from datetime import datetime, timezone
from pathlib import Path

VERSION = "3.1.0"


def utc_now():
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def fingerprint(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Workspace:
    def __init__(self, directory: Path):
        self.directory = directory
        self.warning = ""
        try:
            directory.mkdir(parents=True, exist_ok=True)
            self.db = sqlite3.connect(directory / "workspace.sqlite3", timeout=3)
        except (OSError, sqlite3.Error) as error:
            self.warning = f"训练历史暂存内存：{error}"
            self.db = sqlite3.connect(":memory:")
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.execute("PRAGMA synchronous=FULL")
        self.db.execute("""CREATE TABLE IF NOT EXISTS jobs (
            id TEXT PRIMARY KEY, created TEXT NOT NULL, ended TEXT, status TEXT NOT NULL,
            output TEXT NOT NULL, command TEXT NOT NULL, engine_hash TEXT NOT NULL,
            version TEXT NOT NULL, checkpoint TEXT NOT NULL, pid INTEGER NOT NULL,
            result_hash TEXT, detail TEXT)""")
        self.db.commit()
        # A live second app may own its jobs. Never mark all running jobs interrupted.
        for job in self.jobs():
            if job["status"] == "running" and not self._alive(job["pid"]):
                self.finish(job["id"], "interrupted", "上次应用退出，已有检查点可恢复")

    @staticmethod
    def _alive(pid: int) -> bool:
        if pid <= 0:
            return False
        if os.name == "nt":
            import ctypes

            kernel = ctypes.WinDLL("kernel32", use_last_error=True)
            kernel.OpenProcess.restype = ctypes.c_void_p
            handle = kernel.OpenProcess(0x1000, False, pid)
            if not handle:
                return ctypes.get_last_error() == 5  # access denied is not proof of exit
            code = ctypes.c_ulong()
            try:
                kernel.GetExitCodeProcess(ctypes.c_void_p(handle), ctypes.byref(code))
                return code.value == 259
            finally:
                kernel.CloseHandle(ctypes.c_void_p(handle))
        try:
            os.kill(pid, 0)
            return True
        except PermissionError:
            return True
        except ProcessLookupError:
            return False

    def start(self, output: Path, command: list[str], engine: Path) -> str:
        job_id = uuid.uuid4().hex
        with self.db:
            self.db.execute(
                "INSERT INTO jobs VALUES (?, ?, NULL, 'running', ?, ?, ?, ?, ?, ?, NULL, NULL)",
                (
                    job_id,
                    utc_now(),
                    str(output),
                    json.dumps(command, ensure_ascii=False),
                    fingerprint(engine),
                    VERSION,
                    str(output.with_suffix(".checkpoint")),
                    os.getpid(),
                ),
            )
        return job_id

    def finish(self, job_id: str, status: str, detail: str = "", output: Path | None = None):
        digest = fingerprint(output) if output and output.is_file() else None
        with self.db:
            self.db.execute(
                "UPDATE jobs SET ended=?, status=?, detail=?, result_hash=? WHERE id=?",
                (utc_now(), status, detail, digest, job_id),
            )

    def append_log(self, job_id: str, text: str):
        with (self.directory / f"{job_id}.log").open("a", encoding="utf-8") as log:
            log.write(text)

    def jobs(self):
        self.db.row_factory = sqlite3.Row
        return [
            dict(row)
            for row in self.db.execute("SELECT * FROM jobs ORDER BY created DESC, rowid DESC LIMIT 250")
        ]

    def log(self, job_id: str):
        # Read only a bounded tail for display; complete logs stay on disk.
        path = self.directory / f"{job_id}.log"
        try:
            with path.open("rb") as log:
                log.seek(max(0, path.stat().st_size - 100000))
                return log.read().decode("utf-8", errors="replace")
        except OSError:
            return "暂无日志。"
