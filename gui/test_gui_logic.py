import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
os.environ["QT_QPA_PLATFORM"] = "minimal"

from PyQt6.QtCore import QRect
from PyQt6.QtWidgets import QApplication
from aof2_gui import (
    APP_BACKGROUND, APP_DISPLAY_NAME, AppWindow, CARD_BACKGROUND, CARD_TEXT, HandClass, Kind,
    Mode, SUBTITLE_TEXT, TOP_BAR_BACKGROUND, TOP_BAR_TEXT, contrast_ratio,
    dpi_aware_window_size, load_bundle, range_cell_size_for_layout,
)


class FakeScreen:
    def __init__(self, dpr: float, rect: QRect) -> None:
        self._dpr = dpr
        self._rect = rect

    def devicePixelRatio(self) -> float:
        return self._dpr

    def availableGeometry(self) -> QRect:
        return self._rect


scaled = dpi_aware_window_size(FakeScreen(1.5, QRect(0, 0, 1707, 1030)))
if scaled.width() != 1707 or scaled.height() != 1030:
    raise AssertionError(f"150% scaled 2560x1600 must fit available logical area, got {scaled.width()}x{scaled.height()}")
p2_cell = range_cell_size_for_layout(Mode.P2, scaled.width(), scaled.height())
p3_cell = range_cell_size_for_layout(Mode.P3, scaled.width(), scaled.height())
if p2_cell < 50:
    raise AssertionError(f"2P cells should stay large on 150% 2560x1600, got {p2_cell}")
if not (20 <= p3_cell <= 26):
    raise AssertionError(f"3P cells should fit 3x2 without overflowing 150% screen, got {p3_cell}")
if contrast_ratio(TOP_BAR_TEXT, TOP_BAR_BACKGROUND) < 12.0:
    raise AssertionError("top bar title contrast is too low")
if contrast_ratio(SUBTITLE_TEXT, TOP_BAR_BACKGROUND) < 7.0:
    raise AssertionError("top bar subtitle contrast is too low")
if contrast_ratio(CARD_TEXT, CARD_BACKGROUND) < 12.0:
    raise AssertionError("range card text contrast is too low")
if contrast_ratio(CARD_TEXT, APP_BACKGROUND) < 12.0:
    raise AssertionError("main text contrast is too low")

base = Path(__file__).resolve().parent.parent
bundle = load_bundle(base / "data")
if bundle.s2p is None:
    raise SystemExit(f"2P strategy not loaded: {bundle.s2p_error}")

app = QApplication(sys.argv)
win = AppWindow(bundle)
win._set_mode(Mode.P2)

if win.app_title.text() != APP_DISPLAY_NAME:
    raise AssertionError("top bar title missing")

titles = [grid.title.text() for grid in win.range_grids]
if titles != ["SB ALL IN", "BB CALL"]:
    raise AssertionError(f"wrong 2P range titles: {titles}")

aa = HandClass(12, 12, Kind.PAIR)
if win.range_grids[0].cell_for(aa).text() != "":
    raise AssertionError("range cells must be compact color blocks without text")
if win.range_grids[0].cell_for(aa).width() < 40:
    raise AssertionError("2P range cells are too small for adaptive layout")
if win.range_grids[0].cell_for(aa).toolTip().split()[0] != "AA":
    raise AssertionError("pair tooltip must show hand label")

if hasattr(win, "hand_input"):
    raise AssertionError("pure range browser must not keep hand input")
if hasattr(win, "decision_label"):
    raise AssertionError("pure range browser must not keep decision panel")
if hasattr(win, "btn_pos_sb") or hasattr(win, "action_sb"):
    raise AssertionError("pure range browser must not keep position or action controls")

print("[GUI 2P] range browser passed")
