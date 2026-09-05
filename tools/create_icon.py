"""Render the repository's vector application mark to a Windows icon."""

import os
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
from PySide6.QtGui import QGuiApplication, QIcon  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
application = QGuiApplication([])
image = QIcon(str(ROOT / "assets/app.svg")).pixmap(256, 256).toImage()
target = ROOT / "build/app.ico"
target.parent.mkdir(exist_ok=True)
if image.isNull() or not image.save(str(target), "ICO"):
    raise RuntimeError("Cannot render the application icon")
