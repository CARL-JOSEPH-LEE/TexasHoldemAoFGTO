import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import QApplication
from aof2_gui import AppWindow, load_bundle

base = Path(__file__).resolve().parent.parent
bundle = load_bundle(base / "data")

app = QApplication(sys.argv)
win = AppWindow(bundle)
win.show()

QTimer.singleShot(2500, app.quit)

rc = app.exec()
print(f"GUI launched and closed cleanly, rc={rc}")
sys.exit(rc)
