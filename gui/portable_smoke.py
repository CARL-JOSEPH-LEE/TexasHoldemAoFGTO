"""Opt-in end-to-end release check, activated only by the test environment."""

import json
import time
from pathlib import Path

from PySide6.QtCore import QTimer


def run_smoke(app, window, report_path: Path):
    report_path.parent.mkdir(parents=True, exist_ok=True)
    window.players_input.setCurrentIndex(2)
    window.samples_input.setValue(20000)
    window.eval_input.setValue(10000)
    window.audit_input.setValue(10000)
    window.rake_input.setValue(3)
    window.cap_input.setValue(0.5)
    window.no_flop.setChecked(True)
    start = time.monotonic()
    timer = QTimer(window)
    audit_started = False

    def finish():
        nonlocal audit_started
        if window.process is not None and time.monotonic() - start < 30:
            return
        if (
            not audit_started
            and window.process is None
            and window.current
            and window.current.source == window.training_output
        ):
            audit_started = True
            window.focus_samples.setCurrentIndex(0)
            window.start_hand_audit()
            return
        if window.audit_process is not None and time.monotonic() - start < 45:
            return
        timer.stop()
        doc = window.current
        ok = (
            window.process is None
            and doc is not None
            and doc.source == window.training_output
            and doc.players == 4
            and doc.params.rake_rate == 0.03
            and doc.params.rake_cap == 0.5
            and len(doc.nodes) == 14
            and window.progress.value() == 100
            and doc.training_method == 2
            and doc.audit_samples > 0
            and window.training_output.with_suffix(".checkpoint").is_file()
            and bool(window.audits)
            and window.workspace.jobs()[0]["status"] == "succeeded"
        )
        if ok:
            doc.export_csv(report_path.with_suffix(".csv"))
        report = {
            "ok": ok,
            "modes": sorted({d.players for d in window.documents}),
            "progress": window.progress.value(),
            "status": window.status_label.text(),
            "output": str(window.training_output),
            "training_method": doc.training_method if doc else None,
            "audit_samples": doc.audit_samples if doc else 0,
            "focused_audit": bool(window.audits),
            "job_history": len(window.workspace.jobs()),
            "log": window.log_tail,
        }
        report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
        window.close()
        app.exit(0 if ok else 2)

    def begin():
        window.start_training()
        timer.start(50)

    timer.timeout.connect(finish)
    QTimer.singleShot(0, begin)
