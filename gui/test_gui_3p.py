import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
os.environ["QT_QPA_PLATFORM"] = "minimal"

from PyQt6.QtWidgets import QApplication
from aof2_gui import (
    APP_DISPLAY_NAME, AppWindow, GameParams, HandClass, Kind, Mode, RANGE_GREEN, RANGE_RED,
    Strategy3P, load_bundle,
)

NUM = 169


def mock_3p() -> Strategy3P:
    btn_push = [0.0] * NUM
    sb_call_btn_push = [0.0] * NUM
    sb_push_btn_fold = [0.0] * NUM
    bb_call_3way = [0.0] * NUM
    bb_call_vs_btn_only = [0.0] * NUM
    bb_call_vs_sb_only = [0.0] * NUM

    for r in range(13):
        idx = HandClass(r, r, Kind.PAIR).index()
        btn_push[idx] = 1.0
        sb_call_btn_push[idx] = 1.0 if r >= 4 else 0.0
        sb_push_btn_fold[idx] = 1.0
        bb_call_3way[idx] = 1.0 if r >= 6 else 0.0
        bb_call_vs_btn_only[idx] = 1.0 if r >= 4 else 0.0
        bb_call_vs_sb_only[idx] = 1.0 if r >= 2 else 0.0

    for rh in range(1, 13):
        for rl in range(rh):
            si = HandClass(rh, rl, Kind.SUITED).index()
            oi = HandClass(rh, rl, Kind.OFFSUIT).index()
            btn_push[si] = 1.0 if rh >= 8 else 0.0
            btn_push[oi] = 1.0 if rh >= 10 else 0.0
            sb_call_btn_push[si] = 1.0 if rh >= 11 else 0.0
            sb_call_btn_push[oi] = 1.0 if rh == 12 else 0.0
            sb_push_btn_fold[si] = 1.0 if rh >= 8 else 0.0
            sb_push_btn_fold[oi] = 1.0 if rh >= 10 else 0.0
            bb_call_3way[si] = 1.0 if rh == 12 else 0.0
            bb_call_3way[oi] = 1.0 if rh == 12 and rl >= 10 else 0.0
            bb_call_vs_btn_only[si] = 1.0 if rh >= 11 else 0.0
            bb_call_vs_btn_only[oi] = 1.0 if rh >= 11 and rl >= 8 else 0.0
            bb_call_vs_sb_only[si] = 1.0 if rh >= 10 else 0.0
            bb_call_vs_sb_only[oi] = 1.0 if rh >= 11 else 0.0

    zeros = [0.0] * NUM
    return Strategy3P(
        params=GameParams(sb_blind=0.5, bb_blind=1.0, stack=10.0),
        btn_push=tuple(btn_push),
        sb_call_vs_btn_push=tuple(sb_call_btn_push),
        sb_push_vs_btn_fold=tuple(sb_push_btn_fold),
        bb_call_3way=tuple(bb_call_3way),
        bb_call_vs_btn_only=tuple(bb_call_vs_btn_only),
        bb_call_vs_sb_only=tuple(bb_call_vs_sb_only),
        btn_push_ev=tuple(zeros),
        sb_call_ev_vs_btn_push=tuple(zeros),
        sb_push_ev_vs_btn_fold=tuple(zeros),
        bb_call_ev_3way=tuple(zeros),
        bb_call_ev_vs_btn_only=tuple(zeros),
        bb_call_ev_vs_sb_only=tuple(zeros),
        btn_fold_ev=0.0,
        sb_fold_ev_vs_btn_push=0.0,
        sb_fold_ev_vs_btn_fold=0.0,
        bb_fold_ev=0.0,
        btn_ev=0.05,
        sb_ev=-0.04,
        bb_ev=-0.01,
        exploitability_bb=1e-7,
        iterations=100000,
    )


base = Path(__file__).resolve().parent.parent
bundle = load_bundle(base / "data")
bundle.s3p = mock_3p()
bundle.s3p_error = None

app = QApplication(sys.argv)
win = AppWindow(bundle)
win._set_mode(Mode.P3)

if win.app_title.text() != APP_DISPLAY_NAME:
    raise AssertionError("top bar title missing")

titles = [grid.title.text() for grid in win.range_grids]
expected_titles = [
    "BTN ALL IN",
    "SB CALL",
    "BB CALL vs BTN+SB",
    "SB ALL IN",
    "BB CALL vs SB",
    "BB CALL vs BTN",
]
if titles != expected_titles:
    raise AssertionError(f"wrong 3P range titles: {titles}")

aks = HandClass(12, 11, Kind.SUITED)
ako = HandClass(12, 11, Kind.OFFSUIT)
seven_two_o = HandClass(5, 0, Kind.OFFSUIT)

if win.range_grids[0].cell_for(aks).toolTip().split()[0] != "AKs":
    raise AssertionError("right upper triangle must be suited")
if win.range_grids[0].cell_for(ako).toolTip().split()[0] != "AKo":
    raise AssertionError("left lower triangle must be offsuit")
if win.range_grids[0].cell_for(aks).text() != "":
    raise AssertionError("range cells must be compact color blocks without text")
if win.range_grids[0].cell_for(aks).width() < 20:
    raise AssertionError("3P range cells are too small for 150% scaled 2560x1600 layout")
if f"background: {RANGE_GREEN}" not in win.range_grids[0].cell_for(aks).styleSheet():
    raise AssertionError("BTN AKs must be green in mock range")
if f"background: {RANGE_RED}" not in win.range_grids[0].cell_for(seven_two_o).styleSheet():
    raise AssertionError("BTN 72o must be red in mock range")

aa = HandClass(12, 12, Kind.PAIR)
two_two = HandClass(0, 0, Kind.PAIR)
if f"background: {RANGE_GREEN}" not in win.range_grids[1].cell_for(aa).styleSheet():
    raise AssertionError("SB CALL AA must be green")
if f"background: {RANGE_RED}" not in win.range_grids[1].cell_for(two_two).styleSheet():
    raise AssertionError("SB CALL 22 must be red")

print("[GUI 3P] range browser passed")
