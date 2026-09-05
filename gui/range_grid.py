from __future__ import annotations

from PySide6.QtCore import QPoint, QRectF, QSize, Qt, Signal
from PySide6.QtGui import QColor, QFont, QPainter, QPen
from PySide6.QtWidgets import QToolTip, QWidget
from strategy_data import RANKS, HandClass, Kind, RangeRecord

RANGE_GREEN = "#147d72"
RANGE_RED = "#29425c"


class RangeGridWidget(QWidget):
    handSelected = Signal(int)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.record: RangeRecord | None = None
        self.selected = 12
        self.view_mode = "frequency"
        self.comparison = None
        self.mixed_only = False
        self.setMinimumSize(300, 300)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.setMouseTracking(True)
        self.setAccessibleName("169 种手牌的全下或跟注频率表")

    def sizeHint(self):
        return QSize(560, 560)

    def set_record(self, record: RangeRecord | None):
        self.record = record
        self.update()

    @staticmethod
    def hand_at(row: int, col: int) -> HandClass:
        a, b = 12 - row, 12 - col
        return HandClass.make(a, b, a > b)

    def geometry_for_grid(self):
        cell = min((self.width() - 12) / 14, (self.height() - 12) / 14)
        return cell, (self.width() - cell * 14) / 2, (self.height() - cell * 14) / 2

    def cell_rect_for(self, hand: HandClass) -> QRectF:
        if hand.kind is Kind.OFFSUIT:
            row, col = 12 - hand.rank_low, 12 - hand.rank_high
        else:
            row, col = 12 - hand.rank_high, 12 - hand.rank_low
        cell, x, y = self.geometry_for_grid()
        return QRectF(x + (col + 1) * cell, y + (row + 1) * cell, cell - 2, cell - 2)

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        cell, x, y = self.geometry_for_grid()
        font = QFont("Segoe UI", max(8, int(cell * 0.18)))
        font.setBold(True)
        painter.setFont(font)
        painter.setPen(QColor("#586b7c"))
        for n, rank in enumerate(reversed(RANKS)):
            painter.drawText(
                QRectF(x + (n + 1) * cell, y, cell - 2, cell - 2),
                Qt.AlignmentFlag.AlignCenter,
                rank,
            )
            painter.drawText(
                QRectF(x, y + (n + 1) * cell, cell - 2, cell - 2),
                Qt.AlignmentFlag.AlignCenter,
                rank,
            )
        for row in range(13):
            for col in range(13):
                hand = self.hand_at(row, col)
                idx = hand.index()
                rect = self.cell_rect_for(hand)
                freq = self.record.frequency[idx] if self.record else None
                available = self.record and (
                    self.record.effective_samples is None or self.record.effective_samples[idx] > 0
                )
                value = f"{freq * 100:.0f}%" if freq is not None else "—"
                fill = RANGE_RED if freq is not None else "#617384"
                if self.view_mode == "ev":
                    delta = self.record.action_ev[idx] - self.record.fold_ev[idx] if available else None
                    value = f"{delta:+.2f}" if delta is not None else "—"
                    fill = self.diverging(delta, 2)
                elif self.view_mode == "difference":
                    delta = (
                        (freq - self.comparison.frequency[idx]) * 100
                        if self.comparison and freq is not None
                        else None
                    )
                    value = f"{delta:+.0f}" if delta is not None else "—"
                    fill = self.diverging(delta, 30)
                elif self.view_mode == "confidence":
                    lo = self.record.advantage_lower[idx] if available and self.record.advantage_lower else 0
                    hi = self.record.advantage_upper[idx] if available and self.record.advantage_upper else 0
                    fill, value = (
                        ("#246d96", "行动")
                        if lo > 0
                        else ("#a85d2d", "弃牌")
                        if hi < 0
                        else ("#617384", "未区分")
                    )
                painter.fillRect(rect, QColor(fill))
                if self.view_mode == "frequency" and freq is not None and freq > 0:
                    painter.fillRect(
                        QRectF(rect.x(), rect.y(), rect.width() * freq, rect.height()),
                        QColor(RANGE_GREEN),
                    )
                painter.setPen(QColor("#ffffff" if freq is not None else "#708393"))
                painter.setFont(font)
                label_rect = QRectF(rect.x(), rect.y() + 1, rect.width(), rect.height() * 0.56)
                painter.drawText(label_rect, Qt.AlignmentFlag.AlignCenter, hand.label())
                small = QFont("Segoe UI", max(7, int(cell * 0.145)))
                painter.setFont(small)
                painter.drawText(
                    QRectF(
                        rect.x(),
                        rect.y() + rect.height() * 0.49,
                        rect.width(),
                        rect.height() * 0.45,
                    ),
                    Qt.AlignmentFlag.AlignCenter,
                    value,
                )
                if self.mixed_only and freq is not None and (freq <= 0.001 or freq >= 0.999):
                    painter.fillRect(rect, QColor(225, 234, 242, 185))
                if idx == self.selected:
                    painter.setPen(QPen(QColor("#f8c15c"), 2.5))
                    painter.drawRect(rect.adjusted(1.2, 1.2, -1.2, -1.2))

    @staticmethod
    def diverging(value, scale):
        if value is None:
            return "#617384"
        intensity = min(1.0, abs(value) / scale)
        target = QColor("#246d96" if value >= 0 else "#a85d2d")
        base = QColor("#617384")
        return QColor(
            *(round(a + (b - a) * intensity) for a, b in zip(base.getRgb()[:3], target.getRgb()[:3]))
        ).name()

    def keyPressEvent(self, event):
        keys = {
            Qt.Key.Key_Left: (0, -1),
            Qt.Key.Key_Right: (0, 1),
            Qt.Key.Key_Up: (-1, 0),
            Qt.Key.Key_Down: (1, 0),
        }
        if event.key() in keys:
            current = HandClass.from_index(self.selected)
            row, col = (
                (12 - current.rank_low, 12 - current.rank_high)
                if current.kind is Kind.OFFSUIT
                else (12 - current.rank_high, 12 - current.rank_low)
            )
            dr, dc = keys[event.key()]
            self.select_hand(self.hand_at(max(0, min(12, row + dr)), max(0, min(12, col + dc))).index())
        else:
            super().keyPressEvent(event)

    def index_at(self, point: QPoint) -> int | None:
        cell, x, y = self.geometry_for_grid()
        col = int((point.x() - x) // cell) - 1
        row = int((point.y() - y) // cell) - 1
        return self.hand_at(row, col).index() if 0 <= row < 13 and 0 <= col < 13 else None

    def select_hand(self, index: int):
        HandClass.from_index(index)
        self.selected = index
        self.update()
        self.handSelected.emit(index)

    def mousePressEvent(self, event):
        index = self.index_at(event.position().toPoint())
        if index is not None:
            self.select_hand(index)

    def mouseMoveEvent(self, event):
        index = self.index_at(event.position().toPoint())
        if index is None or self.record is None:
            QToolTip.hideText()
            return
        node = self.record
        text = f"{HandClass.from_index(index).label()}  全下 / 跟注 {node.frequency[index] * 100:.2f}%"
        if node.effective_samples is None or node.effective_samples[index] > 0:
            text += f"\n行动 EV {node.action_ev[index]:+.4f} BB  |  弃牌 EV {node.fold_ev[index]:+.4f} BB"
        else:
            text += "\n该场景评估样本不足，EV 暂不可用"
        QToolTip.showText(event.globalPosition().toPoint(), text, self)
