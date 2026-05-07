import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
os.environ["QT_QPA_PLATFORM"] = "minimal"

from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import QApplication
import aof2_gui

old_show = aof2_gui.AppWindow.show


def show_and_quit(self):
    old_show(self)
    QTimer.singleShot(250, QApplication.instance().quit)


aof2_gui.AppWindow.show = show_and_quit

rc = aof2_gui.main()
print(f"GUI main launched and closed cleanly, rc={rc}")
sys.exit(rc)
