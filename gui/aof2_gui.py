from __future__ import annotations

import os
import struct
import sys
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Optional

from PyQt6.QtCore import QSize, Qt, QTimer
from PyQt6.QtGui import QFont, QPalette, QColor
from PyQt6.QtWidgets import (
    QApplication, QButtonGroup, QFrame, QGridLayout, QHBoxLayout, QLabel,
    QMainWindow, QPushButton, QScrollArea, QVBoxLayout, QWidget,
)

NUM_HAND_CLASSES = 169
NUM_RANKS = 13
RANKS = "23456789TJQKA"

TARGET_PHYSICAL_WINDOW = QSize(2560, 1600)
MIN_LOGICAL_WINDOW = QSize(1280, 800)
ROOT_H_MARGINS = 64
ROOT_V_MARGINS = 60
TOP_BAR_HEIGHT = 104
TOP_BAR_GAP = 26
RANGE_H_GAP = 24
RANGE_V_GAP = 26
RANGE_CARD_H_PADDING = 36
RANGE_CARD_V_PADDING = 32
RANGE_TITLE_HEIGHT = 27
RANGE_TITLE_GAP = 12
RANGE_GRID_GAP = 2
APP_BACKGROUND = "#f1f5f9"
TOP_BAR_BACKGROUND = "#020617"
TOP_BAR_BORDER = "#334155"
TOP_BAR_TEXT = "#ffffff"
SUBTITLE_TEXT = "#e2e8f0"
MUTED_TEXT = "#475569"
CARD_BACKGROUND = "#ffffff"
CARD_BORDER = "#cbd5e1"
CARD_TEXT = "#020617"
RANK_TEXT = "#0f172a"
MODE_IDLE_BACKGROUND = "#e2e8f0"
MODE_IDLE_TEXT = "#0f172a"
MODE_ACTIVE_BACKGROUND = "#0f766e"
MODE_ACTIVE_BORDER = "#0d9488"
MODE_DISABLED_BACKGROUND = "#cbd5e1"
MODE_DISABLED_TEXT = "#64748b"
RANGE_GREEN = "#15803d"
RANGE_RED = "#b91c1c"
RANGE_CELL_BORDER = "#ffffff"
APP_DISPLAY_NAME = "Texas hold'em AoF GTO"

STRAT2_MAGIC = b"AOF2STR2"
STRAT2_VERSION = 2

STRAT3_MAGIC = b"AOF2ST31"
STRAT3_VERSION = 1


class Kind(Enum):
    PAIR = 0
    SUITED = 1
    OFFSUIT = 2


@dataclass(frozen=True)
class HandClass:
    rank_high: int
    rank_low: int
    kind: Kind

    def __post_init__(self) -> None:
        if not (0 <= self.rank_high < 13):
            raise ValueError(f"rank_high out of range: {self.rank_high}")
        if not (0 <= self.rank_low < 13):
            raise ValueError(f"rank_low out of range: {self.rank_low}")
        if self.kind is Kind.PAIR and self.rank_high != self.rank_low:
            raise ValueError("pair requires rank_high == rank_low")
        if self.kind is not Kind.PAIR and self.rank_high <= self.rank_low:
            raise ValueError("non-pair requires rank_high > rank_low")

    def index(self) -> int:
        if self.kind is Kind.PAIR:
            return self.rank_high
        offset = self.rank_high * (self.rank_high - 1) // 2 + self.rank_low
        return (13 if self.kind is Kind.SUITED else 91) + offset

    def label(self) -> str:
        s = RANKS[self.rank_high] + RANKS[self.rank_low]
        if self.kind is Kind.SUITED:
            return s + "s"
        if self.kind is Kind.OFFSUIT:
            return s + "o"
        return s

    def num_combos(self) -> int:
        if self.kind is Kind.PAIR:
            return 6
        if self.kind is Kind.SUITED:
            return 4
        return 12

    @classmethod
    def make(cls, rank_a: int, rank_b: int, suited: bool) -> "HandClass":
        if rank_a == rank_b:
            return cls(rank_a, rank_a, Kind.PAIR)
        rh = max(rank_a, rank_b)
        rl = min(rank_a, rank_b)
        return cls(rh, rl, Kind.SUITED if suited else Kind.OFFSUIT)


class HandParseError(ValueError):
    pass


def parse_hand_input(text: str) -> HandClass:
    if text is None:
        raise HandParseError("empty input")
    s = text.strip().upper()
    if not s:
        raise HandParseError("empty input")
    if len(s) < 2 or len(s) > 3:
        raise HandParseError(
            f"length must be 2 or 3 (got '{text}'); examples: AA, KK, AKs, T9o"
        )
    rank_set = set(RANKS)
    if s[0] not in rank_set:
        raise HandParseError(f"first char '{s[0]}' is not a valid rank (use 23456789TJQKA)")
    if s[1] not in rank_set:
        raise HandParseError(f"second char '{s[1]}' is not a valid rank (use 23456789TJQKA)")

    r1 = RANKS.index(s[0])
    r2 = RANKS.index(s[1])

    if r1 == r2:
        if len(s) != 2:
            raise HandParseError(
                f"pocket pair must be 2 chars only (got '{text}'); "
                "do NOT add 's' or 'o' (a pocket pair is always offsuit)"
            )
        return HandClass.make(r1, r1, False)

    if len(s) != 3:
        raise HandParseError(
            f"non-pair must end with 's' (suited) or 'o' (offsuit); got '{text}'"
        )
    suit_char = s[2]
    if suit_char == "S":
        return HandClass.make(r1, r2, True)
    if suit_char == "O":
        return HandClass.make(r1, r2, False)
    raise HandParseError(f"last char must be 's' or 'o' (got '{s[2]}')")


@dataclass
class GameParams:
    sb_blind: float
    bb_blind: float
    stack: float


@dataclass
class Strategy2P:
    params: GameParams
    sb_push: tuple[float, ...]
    bb_call: tuple[float, ...]
    sb_push_ev: tuple[float, ...]
    bb_call_ev: tuple[float, ...]
    sb_fold_ev: float
    bb_fold_ev: float
    sb_ev: float
    exploitability_bb: float
    iterations: int

    @classmethod
    def load(cls, path: Path) -> "Strategy2P":
        data = path.read_bytes()
        cur = 0
        if data[cur:cur + 8] != STRAT2_MAGIC:
            raise ValueError(f"bad magic in 2P strategy file: {path}")
        cur += 8

        version, n = struct.unpack_from("<II", data, cur)
        cur += 8
        if version != STRAT2_VERSION:
            raise ValueError(f"unsupported 2P strategy version: {version}")
        if n != NUM_HAND_CLASSES:
            raise ValueError(f"hand class count mismatch: {n}")

        sb_blind, bb_blind, stack = struct.unpack_from("<ddd", data, cur)
        cur += 24

        sb_push = struct.unpack_from(f"<{NUM_HAND_CLASSES}d", data, cur)
        cur += 8 * NUM_HAND_CLASSES
        bb_call = struct.unpack_from(f"<{NUM_HAND_CLASSES}d", data, cur)
        cur += 8 * NUM_HAND_CLASSES

        sb_push_ev = struct.unpack_from(f"<{NUM_HAND_CLASSES}d", data, cur)
        cur += 8 * NUM_HAND_CLASSES
        bb_call_ev = struct.unpack_from(f"<{NUM_HAND_CLASSES}d", data, cur)
        cur += 8 * NUM_HAND_CLASSES

        sb_fold_ev, bb_fold_ev = struct.unpack_from("<dd", data, cur)
        cur += 16

        sb_ev, expl = struct.unpack_from("<dd", data, cur)
        cur += 16
        (iterations,) = struct.unpack_from("<Q", data, cur)
        cur += 8

        if cur != len(data):
            raise ValueError(
                f"trailing bytes in 2P strategy file ({len(data) - cur} bytes)"
            )

        return cls(
            params=GameParams(sb_blind=sb_blind, bb_blind=bb_blind, stack=stack),
            sb_push=sb_push,
            bb_call=bb_call,
            sb_push_ev=sb_push_ev,
            bb_call_ev=bb_call_ev,
            sb_fold_ev=sb_fold_ev,
            bb_fold_ev=bb_fold_ev,
            sb_ev=sb_ev,
            exploitability_bb=expl,
            iterations=iterations,
        )


@dataclass
class Strategy3P:
    params: GameParams
    btn_push: tuple[float, ...]
    sb_call_vs_btn_push: tuple[float, ...]
    sb_push_vs_btn_fold: tuple[float, ...]
    bb_call_3way: tuple[float, ...]
    bb_call_vs_btn_only: tuple[float, ...]
    bb_call_vs_sb_only: tuple[float, ...]

    btn_push_ev: tuple[float, ...]
    sb_call_ev_vs_btn_push: tuple[float, ...]
    sb_push_ev_vs_btn_fold: tuple[float, ...]
    bb_call_ev_3way: tuple[float, ...]
    bb_call_ev_vs_btn_only: tuple[float, ...]
    bb_call_ev_vs_sb_only: tuple[float, ...]

    btn_fold_ev: float
    sb_fold_ev_vs_btn_push: float
    sb_fold_ev_vs_btn_fold: float
    bb_fold_ev: float

    btn_ev: float
    sb_ev: float
    bb_ev: float

    exploitability_bb: float
    iterations: int

    @classmethod
    def load(cls, path: Path) -> "Strategy3P":
        data = path.read_bytes()
        cur = 0
        if data[cur:cur + 8] != STRAT3_MAGIC:
            raise ValueError(f"bad magic in 3P strategy file: {path}")
        cur += 8

        version, n = struct.unpack_from("<II", data, cur)
        cur += 8
        if version != STRAT3_VERSION:
            raise ValueError(f"unsupported 3P strategy version: {version}")
        if n != NUM_HAND_CLASSES:
            raise ValueError(f"hand class count mismatch: {n}")

        sb_blind, bb_blind, stack = struct.unpack_from("<ddd", data, cur)
        cur += 24

        def read_arr() -> tuple[float, ...]:
            nonlocal cur
            arr = struct.unpack_from(f"<{NUM_HAND_CLASSES}d", data, cur)
            cur += 8 * NUM_HAND_CLASSES
            return arr

        btn_push            = read_arr()
        sb_call_vs_btn_push = read_arr()
        sb_push_vs_btn_fold = read_arr()
        bb_call_3way        = read_arr()
        bb_call_vs_btn_only = read_arr()
        bb_call_vs_sb_only  = read_arr()

        btn_push_ev            = read_arr()
        sb_call_ev_vs_btn_push = read_arr()
        sb_push_ev_vs_btn_fold = read_arr()
        bb_call_ev_3way        = read_arr()
        bb_call_ev_vs_btn_only = read_arr()
        bb_call_ev_vs_sb_only  = read_arr()

        btn_fold_ev, sb_fold_ev_vs_btn_push, sb_fold_ev_vs_btn_fold, bb_fold_ev = \
            struct.unpack_from("<dddd", data, cur)
        cur += 32

        btn_ev, sb_ev, bb_ev = struct.unpack_from("<ddd", data, cur)
        cur += 24

        expl, = struct.unpack_from("<d", data, cur)
        cur += 8
        (iterations,) = struct.unpack_from("<Q", data, cur)
        cur += 8

        if cur != len(data):
            raise ValueError(
                f"trailing bytes in 3P strategy file ({len(data) - cur} bytes)"
            )

        return cls(
            params=GameParams(sb_blind=sb_blind, bb_blind=bb_blind, stack=stack),
            btn_push=btn_push,
            sb_call_vs_btn_push=sb_call_vs_btn_push,
            sb_push_vs_btn_fold=sb_push_vs_btn_fold,
            bb_call_3way=bb_call_3way,
            bb_call_vs_btn_only=bb_call_vs_btn_only,
            bb_call_vs_sb_only=bb_call_vs_sb_only,
            btn_push_ev=btn_push_ev,
            sb_call_ev_vs_btn_push=sb_call_ev_vs_btn_push,
            sb_push_ev_vs_btn_fold=sb_push_ev_vs_btn_fold,
            bb_call_ev_3way=bb_call_ev_3way,
            bb_call_ev_vs_btn_only=bb_call_ev_vs_btn_only,
            bb_call_ev_vs_sb_only=bb_call_ev_vs_sb_only,
            btn_fold_ev=btn_fold_ev,
            sb_fold_ev_vs_btn_push=sb_fold_ev_vs_btn_push,
            sb_fold_ev_vs_btn_fold=sb_fold_ev_vs_btn_fold,
            bb_fold_ev=bb_fold_ev,
            btn_ev=btn_ev,
            sb_ev=sb_ev,
            bb_ev=bb_ev,
            exploitability_bb=expl,
            iterations=iterations,
        )


class Mode(Enum):
    P2 = 2
    P3 = 3


class Position(Enum):
    SB = "SB"
    BB = "BB"
    BTN = "BTN"


class PriorAction(Enum):
    NONE = "none"
    SB_PUSH = "sb_push"
    SB_FOLD = "sb_fold"
    BTN_PUSH = "btn_push"
    BTN_FOLD = "btn_fold"
    BTN_P_SB_C = "btn_push_sb_call"
    BTN_P_SB_F = "btn_push_sb_fold"
    BTN_F_SB_P = "btn_fold_sb_push"
    BTN_F_SB_F = "btn_fold_sb_fold"


@dataclass
class StrategyBundle:
    s2p: Optional[Strategy2P] = None
    s3p: Optional[Strategy3P] = None
    s2p_error: Optional[str] = None
    s3p_error: Optional[str] = None


def _latest_existing(paths: list[Path]) -> Optional[Path]:
    existing = [p for p in paths if p.exists()]
    if not existing:
        return None
    return max(existing, key=lambda p: p.stat().st_mtime)


def find_2p_strategy_file(data_dir: Path) -> Optional[Path]:
    exact = data_dir / "strategy.bin"
    if exact.exists():
        return exact
    return _latest_existing(sorted(
        p for p in data_dir.glob("strategy_*.bin")
        if not p.name.startswith("strategy_3p_")
    ))


def find_3p_strategy_file(data_dir: Path) -> Optional[Path]:
    exact = data_dir / "strategy_3p.bin"
    if exact.exists():
        return exact
    return _latest_existing(sorted(data_dir.glob("strategy_3p_*.bin")))


def load_bundle(data_dir: Path) -> StrategyBundle:
    bundle = StrategyBundle()
    p2 = find_2p_strategy_file(data_dir)
    p3 = find_3p_strategy_file(data_dir)
    if p2 is not None:
        try:
            bundle.s2p = Strategy2P.load(p2)
        except Exception as e:
            bundle.s2p_error = f"{p2.name}: {e}"
    else:
        bundle.s2p_error = (
            f"No 2P strategy found in {data_dir}. Expected strategy.bin or strategy_<STACK>bb.bin."
        )
    if p3 is not None:
        try:
            bundle.s3p = Strategy3P.load(p3)
        except Exception as e:
            bundle.s3p_error = f"{p3.name}: {e}"
    else:
        bundle.s3p_error = (
            f"No 3P strategy found in {data_dir}. Expected strategy_3p.bin or strategy_3p_<STACK>bb.bin."
        )
    return bundle


def runtime_base_dir() -> Path:
    if getattr(sys, "frozen", False):
        bundled_dir = getattr(sys, "_MEIPASS", None)
        if bundled_dir:
            return Path(bundled_dir)
        return Path(sys.executable).resolve().parent
    return Path(__file__).resolve().parent.parent


def dpi_aware_window_size(screen: object | None) -> QSize:
    if screen is None:
        return QSize(TARGET_PHYSICAL_WINDOW)
    dpr = float(screen.devicePixelRatio())
    if dpr <= 0:
        raise ValueError(f"invalid device pixel ratio: {dpr}")
    target_w = round(TARGET_PHYSICAL_WINDOW.width() / dpr)
    target_h = round(TARGET_PHYSICAL_WINDOW.height() / dpr)
    available = screen.availableGeometry()
    return QSize(
        _fit_dimension(target_w, available.width(), MIN_LOGICAL_WINDOW.width()),
        _fit_dimension(target_h, available.height(), MIN_LOGICAL_WINDOW.height()),
    )


def _fit_dimension(target: int, available: int, minimum: int) -> int:
    if available <= 0:
        return max(target, minimum)
    if available < minimum:
        return available
    return min(max(target, minimum), available)


def contrast_ratio(foreground: str, background: str) -> float:
    fg = _relative_luminance(foreground)
    bg = _relative_luminance(background)
    light = max(fg, bg)
    dark = min(fg, bg)
    return (light + 0.05) / (dark + 0.05)


def _relative_luminance(hex_color: str) -> float:
    color = hex_color.lstrip("#")
    if len(color) != 6:
        raise ValueError(f"invalid hex color: {hex_color}")
    channels = [int(color[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]
    linear = []
    for channel in channels:
        if channel <= 0.03928:
            linear.append(channel / 12.92)
        else:
            linear.append(((channel + 0.055) / 1.055) ** 2.4)
    return 0.2126 * linear[0] + 0.7152 * linear[1] + 0.0722 * linear[2]


def range_cell_size_for_layout(mode: Mode, window_width: int, window_height: int) -> int:
    columns = 2 if mode is Mode.P2 else 3
    rows = 1 if mode is Mode.P2 else 2
    content_w = max(1, window_width - ROOT_H_MARGINS)
    content_h = max(1, window_height - ROOT_V_MARGINS - TOP_BAR_HEIGHT - TOP_BAR_GAP)
    card_w = (content_w - (columns - 1) * RANGE_H_GAP) // columns
    card_h = (content_h - (rows - 1) * RANGE_V_GAP) // rows
    grid_gaps = (NUM_RANKS) * RANGE_GRID_GAP
    by_w = (card_w - RANGE_CARD_H_PADDING - grid_gaps) // (NUM_RANKS + 1)
    by_h = (
        card_h - RANGE_CARD_V_PADDING - RANGE_TITLE_HEIGHT - RANGE_TITLE_GAP - grid_gaps
    ) // (NUM_RANKS + 1)
    cap = 64 if mode is Mode.P2 else 48
    floor = 40 if mode is Mode.P2 else 20
    return max(floor, min(cap, by_w, by_h))


class RangeGridWidget(QWidget):
    def __init__(self, cell_size: int = 44, parent: Optional[QWidget] = None) -> None:
        super().__init__(parent)
        self.setObjectName("RangeGridCard")
        self.setStyleSheet(
            "QWidget#RangeGridCard {"
            f"background: {CARD_BACKGROUND}; border: 1px solid {CARD_BORDER}; border-radius: 14px;"
            "}"
        )
        outer = QVBoxLayout(self)
        outer.setContentsMargins(18, 14, 18, 18)
        outer.setSpacing(RANGE_TITLE_GAP)

        self.title = QLabel("")
        self.title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.title.setStyleSheet(f"font-size: 22px; font-weight: 900; color: {CARD_TEXT};")
        outer.addWidget(self.title)

        grid = QGridLayout()
        grid.setSpacing(2)
        outer.addLayout(grid)
        self._cells: dict[int, QLabel] = {}
        header_size = cell_size
        header_font = max(11, cell_size // 3)

        ranks_desc = list(range(NUM_RANKS - 1, -1, -1))
        for pos, rank in enumerate(ranks_desc, start=1):
            top = QLabel(RANKS[rank])
            left = QLabel(RANKS[rank])
            for header in (top, left):
                header.setAlignment(Qt.AlignmentFlag.AlignCenter)
                header.setFixedSize(header_size, header_size)
                header.setStyleSheet(
                    f"font-size: {header_font}px; font-weight: 900; color: {RANK_TEXT};"
                )
            grid.addWidget(top, 0, pos)
            grid.addWidget(left, pos, 0)

        for row_rank in ranks_desc:
            for col_rank in ranks_desc:
                hand = self._hand_for_cell(row_rank, col_rank)
                cell = QLabel("")
                cell.setAlignment(Qt.AlignmentFlag.AlignCenter)
                cell.setFixedSize(cell_size, cell_size)
                grid.addWidget(cell, NUM_RANKS - row_rank, NUM_RANKS - col_rank)
                self._cells[hand.index()] = cell

    def cell_for(self, hand: HandClass) -> QLabel:
        return self._cells[hand.index()]

    def set_range(self, title: str, freqs: tuple[float, ...] | list[float]) -> None:
        self.title.setText(title)
        for idx, freq in enumerate(freqs):
            hand = HandClass.from_index(idx) if hasattr(HandClass, "from_index") else None
            if hand is None:
                hand = self._hand_from_index(idx)
            cell = self._cells[idx]
            cell.setText("")
            cell.setToolTip(f"{hand.label()} {freq * 100:.1f}%")
            bg = RANGE_GREEN if freq >= 0.5 else RANGE_RED
            cell.setStyleSheet(
                "QLabel {"
                f"background: {bg}; color: {TOP_BAR_TEXT}; border: 1px solid {RANGE_CELL_BORDER};"
                "}"
            )

    @staticmethod
    def _hand_for_cell(row_rank: int, col_rank: int) -> HandClass:
        if row_rank == col_rank:
            return HandClass(row_rank, row_rank, Kind.PAIR)
        if row_rank > col_rank:
            return HandClass(row_rank, col_rank, Kind.SUITED)
        return HandClass(col_rank, row_rank, Kind.OFFSUIT)

    @staticmethod
    def _hand_from_index(idx: int) -> HandClass:
        if idx < 13:
            return HandClass(idx, idx, Kind.PAIR)
        kind = Kind.SUITED if idx < 91 else Kind.OFFSUIT
        offset = idx - (13 if kind is Kind.SUITED else 91)
        rh = 1
        while rh * (rh - 1) // 2 + (rh - 1) < offset:
            rh += 1
        rl = offset - rh * (rh - 1) // 2
        return HandClass(rh, rl, kind)


class AppWindow(QMainWindow):
    def __init__(self, bundle: StrategyBundle) -> None:
        super().__init__()
        self.bundle = bundle
        self.mode = Mode.P2 if bundle.s2p is not None else Mode.P3

        self.setWindowTitle(APP_DISPLAY_NAME)
        self.resize(dpi_aware_window_size(QApplication.primaryScreen()))
        self._build_ui()
        self._apply_mode()

    def _build_ui(self) -> None:
        root = QWidget(self)
        self.setCentralWidget(root)
        root.setStyleSheet(f"background: {APP_BACKGROUND};")
        root_layout = QVBoxLayout(root)
        root_layout.setContentsMargins(32, 28, 32, 32)
        root_layout.setSpacing(TOP_BAR_GAP)

        top_bar = QWidget()
        top_bar.setObjectName("TopBar")
        top_bar.setStyleSheet(
            "QWidget#TopBar {"
            f"background: {TOP_BAR_BACKGROUND}; border: 1px solid {TOP_BAR_BORDER}; border-radius: 16px;"
            "}"
        )
        top_bar.setFixedHeight(TOP_BAR_HEIGHT)
        mode_row = QHBoxLayout()
        mode_row.setContentsMargins(28, 16, 28, 16)
        top_bar.setLayout(mode_row)

        title_block = QVBoxLayout()
        title_block.setSpacing(4)
        self.app_title = QLabel(APP_DISPLAY_NAME)
        self.app_title.setStyleSheet(f"font-size: 30px; font-weight: 900; color: {TOP_BAR_TEXT};")
        self.app_subtitle = QLabel("10BB Nash ranges - green = ALL IN / CALL, red = FOLD")
        self.app_subtitle.setStyleSheet(f"font-size: 16px; font-weight: 800; color: {SUBTITLE_TEXT};")
        title_block.addWidget(self.app_title)
        title_block.addWidget(self.app_subtitle)
        mode_row.addLayout(title_block, 1)

        self.mode_label = QLabel("Mode")
        self.mode_label.setStyleSheet(f"font-size: 13px; font-weight: 900; color: {SUBTITLE_TEXT};")
        mode_row.addWidget(self.mode_label)

        self.btn_mode_2p = QPushButton("2P")
        self.btn_mode_3p = QPushButton("3P")
        for btn in (self.btn_mode_2p, self.btn_mode_3p):
            btn.setCheckable(True)
            btn.setFixedSize(84, 46)
            btn.setStyleSheet(
                "QPushButton {"
                f"background: {MODE_IDLE_BACKGROUND}; color: {MODE_IDLE_TEXT}; border: 1px solid {CARD_BORDER};"
                "border-radius: 10px; font-size: 17px; font-weight: 900;"
                "}"
                f"QPushButton:checked {{ background: {MODE_ACTIVE_BACKGROUND}; color: {TOP_BAR_TEXT}; border: 1px solid {MODE_ACTIVE_BORDER}; }}"
                f"QPushButton:disabled {{ background: {MODE_DISABLED_BACKGROUND}; color: {MODE_DISABLED_TEXT}; border: 1px solid {CARD_BORDER}; }}"
            )
        mode_grp = QButtonGroup(self)
        mode_grp.addButton(self.btn_mode_2p)
        mode_grp.addButton(self.btn_mode_3p)
        mode_grp.setExclusive(True)
        self.btn_mode_2p.clicked.connect(lambda: self._set_mode(Mode.P2))
        self.btn_mode_3p.clicked.connect(lambda: self._set_mode(Mode.P3))
        mode_row.addWidget(self.btn_mode_2p)
        mode_row.addWidget(self.btn_mode_3p)
        root_layout.addWidget(top_bar)

        right_scroll = QScrollArea()
        right_scroll.setWidgetResizable(True)
        right_scroll.setFrameShape(QFrame.Shape.NoFrame)
        right_scroll.setStyleSheet("QScrollArea { background: transparent; border: none; }")
        right_panel = QWidget()
        right_panel.setStyleSheet("background: transparent;")
        right_scroll.setWidget(right_panel)
        self.range_layout = QGridLayout(right_panel)
        self.range_layout.setContentsMargins(0, 0, 0, 0)
        self.range_layout.setHorizontalSpacing(RANGE_H_GAP)
        self.range_layout.setVerticalSpacing(RANGE_V_GAP)
        self.range_layout.setAlignment(Qt.AlignmentFlag.AlignTop | Qt.AlignmentFlag.AlignHCenter)
        self.range_grids: list[RangeGridWidget] = []
        root_layout.addWidget(right_scroll, 1)

    def _make_section_label(self, text: str) -> QLabel:
        lbl = QLabel(text)
        f = QFont()
        f.setBold(True)
        f.setPointSize(9)
        lbl.setFont(f)
        lbl.setStyleSheet("color: #1f2937; margin-top: 2px;")
        return lbl

    def _set_mode(self, mode: Mode) -> None:
        self.mode = mode
        self._apply_mode()

    def _apply_mode(self) -> None:
        self.btn_mode_2p.setEnabled(self.bundle.s2p is not None)
        self.btn_mode_3p.setEnabled(self.bundle.s3p is not None)
        self.btn_mode_2p.setChecked(self.mode is Mode.P2)
        self.btn_mode_3p.setChecked(self.mode is Mode.P3)
        self._render_ranges()

    def _render_ranges(self) -> None:
        self._clear_ranges()
        if self.mode is Mode.P2:
            s2 = self.bundle.s2p
            if s2 is None:
                self._add_error(f"2P strategy missing: {self.bundle.s2p_error}")
                return
            ranges = [
                ("SB ALL IN", s2.sb_push),
                ("BB CALL", s2.bb_call),
            ]
            columns = 2
        else:
            s3 = self.bundle.s3p
            if s3 is None:
                self._add_error(f"3P strategy missing: {self.bundle.s3p_error}")
                return
            ranges = [
                ("BTN ALL IN", s3.btn_push),
                ("SB CALL", s3.sb_call_vs_btn_push),
                ("BB CALL vs BTN+SB", s3.bb_call_3way),
                ("SB ALL IN", s3.sb_push_vs_btn_fold),
                ("BB CALL vs SB", s3.bb_call_vs_sb_only),
                ("BB CALL vs BTN", s3.bb_call_vs_btn_only),
            ]
            columns = 3
        cell_size = range_cell_size_for_layout(self.mode, self.width(), self.height())
        for i, (title, freqs) in enumerate(ranges):
            self._add_range(title, freqs, i // columns, i % columns, cell_size)

    def _clear_ranges(self) -> None:
        while self.range_layout.count():
            item = self.range_layout.takeAt(0)
            widget = item.widget()
            if widget is not None:
                widget.deleteLater()
        self.range_grids = []

    def _add_range(
        self,
        title: str,
        freqs: tuple[float, ...] | list[float],
        row: int,
        col: int,
        cell_size: int,
    ) -> None:
        grid = RangeGridWidget(cell_size=cell_size)
        grid.set_range(title, freqs)
        self.range_layout.addWidget(grid, row, col)
        self.range_grids.append(grid)

    def _add_error(self, text: str) -> None:
        label = QLabel(text)
        label.setWordWrap(True)
        label.setStyleSheet(f"font-size: 16px; color: {RANGE_RED}; font-weight: 900;")
        self.range_layout.addWidget(label, 0, 0)

def main() -> int:
    base = runtime_base_dir()
    bundle = load_bundle(base / "data")
    if os.environ.get("AOF2_GUI_TEST_REQUIRE_DATA") and (
        bundle.s2p is None or bundle.s3p is None
    ):
        print(f"2P strategy not loaded: {bundle.s2p_error}", file=sys.stderr)
        print(f"3P strategy not loaded: {bundle.s3p_error}", file=sys.stderr)
        return 2

    app = QApplication(sys.argv)
    palette = app.palette()
    palette.setColor(QPalette.ColorRole.Window, QColor("#fafafa"))
    app.setPalette(palette)

    win = AppWindow(bundle)
    win.show()
    exit_after_ms = os.environ.get("AOF2_GUI_TEST_EXIT_MS")
    if exit_after_ms:
        QTimer.singleShot(max(0, int(exit_after_ms)), app.quit)
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
