from __future__ import annotations

import json
import logging
import os
import re
import sqlite3
import struct
import sys
from dataclasses import dataclass, field
from datetime import datetime
from enum import Enum
from logging.handlers import RotatingFileHandler
from pathlib import Path

from analysis_tools import AnalysisTools
from PySide6.QtCore import QLocale, QProcess, QSettings, QStandardPaths, Qt, QTimer
from PySide6.QtGui import QFont, QIcon
from PySide6.QtWidgets import (
    QApplication,
    QButtonGroup,
    QCheckBox,
    QComboBox,
    QDoubleSpinBox,
    QFileDialog,
    QFormLayout,
    QFrame,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QPlainTextEdit,
    QProgressBar,
    QPushButton,
    QScrollArea,
    QSizePolicy,
    QSpinBox,
    QSplitter,
    QTabWidget,
    QTreeWidget,
    QVBoxLayout,
    QWidget,
)
from range_grid import RangeGridWidget
from strategy_data import (
    GameParams,
    HandClass,
    HandParseError,
    Strategy2P,
    Strategy3P,
    StrategyDocument,
    legacy_document,
    load_strategy,
    parse_hand_input,
    positions,
)
from workspace import VERSION, Workspace, fingerprint

APP_DISPLAY_NAME = "Texas Hold’em AoF Studio"


class Mode(Enum):
    P2 = 2
    P3 = 3
    P4 = 4


class SeedInput(QLineEdit):
    def __init__(self, value=2026):
        super().__init__(str(value))
        self.setMaxLength(20)
        self.setToolTip("0 到 18446744073709551615 的整数；相同配置和种子可复现训练。")

    def value(self):
        text = self.text()
        if not text or not text.isascii() or not text.isdigit() or int(text) > 2**64 - 1:
            raise ValueError("随机种子必须是 0 到 18446744073709551615 的整数")
        return int(text)

    def setValue(self, value):
        self.setText(str(value))
        self.value()


@dataclass
class StrategyBundle:
    s2p: Strategy2P | None = None
    s3p: Strategy3P | None = None
    s2p_error: str | None = None
    s3p_error: str | None = None
    documents: list[StrategyDocument] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)


def runtime_base_dir() -> Path:
    if getattr(sys, "frozen", False):
        return Path(getattr(sys, "_MEIPASS", Path(sys.executable).resolve().parent))
    return Path(__file__).resolve().parent.parent


def user_strategy_dir() -> Path:
    if os.environ.get("AOF_STRATEGY_DIR"):
        return Path(os.environ["AOF_STRATEGY_DIR"]).expanduser().resolve()
    if getattr(sys, "frozen", False):
        return (
            Path(QStandardPaths.writableLocation(QStandardPaths.StandardLocation.AppDataLocation))
            / "strategies"
        )
    return runtime_base_dir() / "data" / "local"


def find_strategy_file(data_dir: Path, magic: bytes) -> Path | None:
    candidates = []
    for path in data_dir.glob("strategy*.bin"):
        try:
            with path.open("rb") as f:
                if f.read(8) == magic:
                    candidates.append(path)
        except OSError:
            continue
    return max(candidates, key=lambda p: p.stat().st_mtime, default=None)


def find_2p_strategy_file(data_dir):
    return find_strategy_file(data_dir, b"AOF2STR2")


def find_3p_strategy_file(data_dir):
    return find_strategy_file(data_dir, b"AOF2ST31")


def load_bundle(data_dir: Path, extra_dirs: tuple[Path, ...] = ()) -> StrategyBundle:
    bundle = StrategyBundle()
    seen = set()
    for directory in (data_dir, *extra_dirs):
        manifest = {}
        manifest_file = directory / "bundle-manifest.json"
        try:
            if manifest_file.is_file():
                if manifest_file.stat().st_size > 100000:
                    raise ValueError("内置策略清单过大")
                manifest = json.loads(manifest_file.read_text(encoding="utf-8"))
                if not isinstance(manifest, dict):
                    raise ValueError("策略清单格式错误")
            elif getattr(sys, "frozen", False) and directory == runtime_base_dir() / "data":
                raise ValueError("内置策略校验清单缺失")
        except (OSError, ValueError) as error:
            bundle.errors.append(str(error))
            continue
        for path in sorted(directory.glob("strategy*.bin")):
            if path.resolve() in seen:
                continue
            seen.add(path.resolve())
            try:
                if path.name in manifest and fingerprint(path) != manifest[path.name]:
                    raise ValueError("内置策略指纹校验失败")
                doc = load_strategy(path)
                bundle.documents.append(doc)
                if doc.engine.startswith("旧版"):
                    if doc.players == 2:
                        bundle.s2p = Strategy2P.load(path)
                    if doc.players == 3:
                        bundle.s3p = Strategy3P.load(path)
            except (OSError, ValueError, struct.error) as e:
                bundle.errors.append(f"{path.name}: {e}")
    if bundle.s2p is None:
        bundle.s2p_error = "未找到旧版两人策略"
    if bundle.s3p is None:
        bundle.s3p_error = "未找到旧版三人策略"
    return bundle


def trainer_path() -> Path | None:
    root = runtime_base_dir()
    name = "aof2_train.exe" if os.name == "nt" else "aof2_train"
    paths = [
        root / "bin" / name,
        root / "build" / name,
        root / "build" / "Release" / name,
        root / name,
    ]
    if os.environ.get("AOF_TRAINER"):
        paths.insert(0, Path(os.environ["AOF_TRAINER"]))
    return next((p.resolve() for p in paths if p.is_file()), None)


def label(text: str, name: str = "", wrap: bool = False) -> QLabel:
    widget = QLabel(text)
    widget.setTextFormat(Qt.TextFormat.PlainText)
    widget.setObjectName(name)
    widget.setWordWrap(wrap)
    return widget


STYLE = """
    QMainWindow,
    QTabWidget,
    QTreeWidget,
    QTreeWidgetItem,
    QPlainTextEdit,
    QSplitter,
    QSizePolicy, QWidget#Root { background: #edf2f5; color: #162c3d; }
    QWidget { font-family: 'Microsoft YaHei UI', 'Segoe UI'; font-size: 12px; color: #162c3d; }
    QFrame#Header { background: #102c3b; border-radius: 14px; }
    QLabel#Brand { color: #ffffff; font-size: 24px; font-weight: 700; }
    QLabel#SubBrand { color: #aec7d2; font-size: 12px; }
    QFrame#Card { background: #ffffff; border: 1px solid #d9e3e9; border-radius: 12px; }
    QLabel#Section { font-weight: 700; color: #173447; font-size: 16px; }
    QLabel#Muted, QLabel#Status { color: #5b7182; }
    QLabel#Metric { color: #16734c; font-size: 23px; font-weight: 700; }
    QPushButton { background: #ffffff; color: #163a50; padding: 8px 12px;
                  border: 1px solid #cbd9e2; border-radius: 7px; font-weight: 600; }
    QPushButton:hover { background: #e9f4f0; border-color: #63a58c; }
    QPushButton:checked, QPushButton#Primary { background: #147955; color: #ffffff; border-color: #147955; }
    QPushButton:disabled { color: #899da9; background: #e9eff2; border-color: #d9e3e9; }
    QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox { background: #f8fafb; color: #162c3d;
         border: 1px solid #cbd9e2; border-radius: 6px; padding: 6px; min-height: 19px; }
    QComboBox QAbstractItemView { background: #ffffff; color: #162c3d; selection-background-color: #d9efe4; }
    QProgressBar { background: #e7eff3; border: none; border-radius: 4px; text-align: center; color: #163a50; }
    QProgressBar::chunk { background: #57b492; border-radius: 4px; }
    QCheckBox { color: #163a50; spacing: 7px; }
    QScrollArea { border: none; background: transparent; }
"""


class AppWindow(AnalysisTools, QMainWindow):
    def __init__(self, bundle: StrategyBundle):
        super().__init__()
        self.setLocale(QLocale.c())
        self.bundle = bundle
        self.documents = list(bundle.documents)
        if not self.documents:
            for s in (bundle.s2p, bundle.s3p):
                if s is not None:
                    self.documents.append(legacy_document(s, Path("内置策略")))
        self.mode = Mode.P4
        self.current: StrategyDocument | None = None
        self.process: QProcess | None = None
        self.training_output: Path | None = None
        self.cancelled = False
        self.process_buffer = ""
        self.log_tail = ""
        self.workspace = Workspace(user_strategy_dir())
        self.job_id = None
        self.audit_process = None
        self.audit_buffer = ""
        self.audits = {}
        self.comparison = None
        self.setWindowTitle(APP_DISPLAY_NAME)
        self.setWindowIcon(QIcon(str(runtime_base_dir() / "assets/app.svg")))
        screen = QApplication.primaryScreen()
        area = screen.availableGeometry() if screen else None
        self.resize(
            min(1440, area.width()) if area else 1440,
            min(980, area.height()) if area else 980,
        )
        self._build_ui()
        mode = (
            Mode.P4
            if any(d.players == 4 for d in self.documents) or not self.documents
            else Mode(self.documents[0].players)
        )
        self._set_mode(mode)
        if self.workspace.warning:
            self.status_label.setText(self.workspace.warning)
        if bundle.errors:
            self.status_label.setText("部分策略未载入：" + "；".join(bundle.errors))

    def _build_ui(self):
        self.setStyleSheet(STYLE)
        root = QWidget()
        root.setObjectName("Root")
        self.setCentralWidget(root)
        layout = QVBoxLayout(root)
        layout.setContentsMargins(16, 12, 16, 10)
        layout.setSpacing(10)
        header = QFrame()
        header.setObjectName("Header")
        row = QHBoxLayout(header)
        row.setContentsMargins(20, 10, 20, 10)
        brand = QVBoxLayout()
        brand.setSpacing(4)
        self.app_title = label(APP_DISPLAY_NAME, "Brand")
        brand.addWidget(self.app_title)
        brand.addWidget(label(f"本地策略研究工作台   ·   2–4 人   ·   v{VERSION}", "SubBrand"))
        row.addLayout(brand)
        row.addStretch()
        self.theme_combo = QComboBox()
        self.theme_combo.addItems(["浅色", "深色"])
        self.theme_combo.currentIndexChanged.connect(self._apply_theme)
        row.addWidget(self.theme_combo)
        group = QButtonGroup(self)
        self.mode_buttons = {}
        for mode in Mode:
            button = QPushButton(f"{mode.value} 人")
            button.setCheckable(True)
            button.setMinimumWidth(68)
            button.clicked.connect(lambda checked, m=mode: self._set_mode(m))
            group.addButton(button)
            row.addWidget(button)
            self.mode_buttons[mode] = button
        self.btn_mode_2p, self.btn_mode_3p, self.btn_mode_4p = (self.mode_buttons[m] for m in Mode)
        layout.addWidget(header)
        body = QHBoxLayout()
        body.setSpacing(16)
        layout.addLayout(body, 1)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFixedWidth(270)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        controls = QFrame()
        controls.setObjectName("Card")
        scroll.setWidget(controls)
        left = QVBoxLayout(controls)
        left.setContentsMargins(14, 14, 14, 14)
        left.setSpacing(12)
        left.addWidget(label("训练设置", "Section"))
        left.addWidget(label("修改参数后点击开始训练，生成对应规则的范围。", "Muted", True))
        self.config_widget = QWidget()
        form = QFormLayout(self.config_widget)
        self.rules_form = form
        form.setContentsMargins(0, 0, 0, 0)
        form.setVerticalSpacing(6)
        form.setFieldGrowthPolicy(QFormLayout.FieldGrowthPolicy.AllNonFixedFieldsGrow)
        self.players_input = QComboBox()
        for n in (2, 3, 4):
            self.players_input.addItem(f"{n} 人", n)
        self.players_input.setCurrentIndex(2)
        self.stack_input = self._double(1.01, 1000000, 10, " BB")
        self.sb_input = self._double(0.001, 1000000, 0.5, " BB", 3)
        self.bb_input = self._double(0.001, 1000000, 1, " BB", 3)
        self.rake_input = self._double(0, 100, 3, " %", 3)
        self.rake_mode_input = QComboBox()
        self.rake_mode_input.addItem("按底池比例", "percentage")
        self.rake_mode_input.addItem("固定金额", "fixed")
        self.fixed_rake_input = self._double(0, 1000000, 0.5, " BB", 3)
        self.fixed_rake_input.setToolTip(
            "每个符合抽水条件的底池只收取一次固定金额；超过实际底池时按底池金额收取。"
            "“无人跟注不抽水”开关仍然生效。"
        )
        self.cap_input = self._double(0, 1000000, 0, " BB", 3)
        self.cap_input.setSpecialValueText("不封顶")
        for text, w in (
            ("人数", self.players_input),
            ("总筹码", self.stack_input),
            ("小盲", self.sb_input),
            ("大盲", self.bb_input),
            ("抽水方式", self.rake_mode_input),
            ("底池抽水", self.rake_input),
            ("每局封顶", self.cap_input),
            ("固定抽水", self.fixed_rake_input),
        ):
            form.addRow(text, w)
        self.rake_mode_input.currentIndexChanged.connect(self._update_rake_mode)
        self._update_rake_mode()
        self.algorithm_input = QComboBox()
        for title, value in (("线性 CFR", "none"), ("DCFR（实验）", "dcfr"), ("HS-DCFR（实验）", "hs-dcfr")):
            self.algorithm_input.addItem(title, value)
        form.addRow("训练算法", self.algorithm_input)
        self.algorithm_input.setToolTip(
            "默认线性 CFR。在本机四人抽水基准中，DCFR 与 HS-DCFR 未优于默认算法。"
        )
        self.batch_input = self._integer(1, 1024, 4)
        self.batch_input.setToolTip(
            "每次策略更新中，每个分层抽取的样本数。较大值减少更新次数并降低单次更新噪声。"
        )
        form.addRow("每层样本", self.batch_input)
        self.no_flop = QCheckBox("无人跟注不抽水")
        self.no_flop.setChecked(True)
        form.addRow(self.no_flop)
        self.samples_input = self._integer(1000, 1000000000, 10000000, 1000000)
        self.eval_input = self._integer(1000, 100000000, 1000000, 100000)
        self.samples_input.setValue(100000000)
        self.audit_input = self._integer(1000, 1000000000, 20000000, 1000000)
        self.seed_input = SeedInput()
        self.threads_input = self._integer(0, 256, 0)
        self.threads_input.setSpecialValueText("自动")
        for text, w in (
            ("训练采样预算", self.samples_input),
            ("全局评估样本", self.eval_input),
            ("逐手精度样本", self.audit_input),
            ("随机种子", self.seed_input),
            ("线程数", self.threads_input),
        ):
            form.addRow(text, w)
        left.addWidget(self.config_widget)
        self.train_button = QPushButton("开始训练")
        self.train_button.setObjectName("Primary")
        self.train_button.clicked.connect(self.start_training)
        left.addWidget(self.train_button)
        self.resume_button = QPushButton("从检查点继续训练")
        self.resume_button.setToolTip("沿用检查点规则和随机状态，追加上方的训练采样预算。")
        self.resume_button.clicked.connect(self.resume_training)
        left.addWidget(self.resume_button)
        self.cancel_button = QPushButton("取消训练")
        self.cancel_button.setEnabled(False)
        self.cancel_button.clicked.connect(self.cancel_training)
        left.addWidget(self.cancel_button)
        self.progress = QProgressBar()
        self.progress.setRange(0, 100)
        self.progress.setValue(0)
        left.addWidget(self.progress)
        self.training_label = label("就绪。抽水按整局底池计算一次。", "Muted", True)
        left.addWidget(self.training_label)
        left.addWidget(
            label(
                "所有座位使用相同总筹码，已含盲注。\n未被跟注的多余下注不参与抽水。",
                "Muted",
                True,
            )
        )
        left.addStretch()
        left_column = QFrame()
        left_column.setFixedWidth(270)
        left_layout = QVBoxLayout(left_column)
        left_layout.setContentsMargins(0, 0, 0, 0)
        left_layout.addWidget(scroll, 1)
        for widget in (
            self.train_button,
            self.resume_button,
            self.cancel_button,
            self.progress,
            self.training_label,
        ):
            left.removeWidget(widget)
            left_layout.addWidget(widget)
        body.addWidget(left_column)
        self.tabs = QTabWidget()
        self.tabs.setDocumentMode(True)
        body.addWidget(self.tabs, 1)
        right = QFrame()
        right.setObjectName("Card")
        self.tabs.addTab(right, "策略分析")
        panel = QVBoxLayout(right)
        panel.setContentsMargins(14, 12, 14, 12)
        panel.setSpacing(7)
        toolbar = QHBoxLayout()
        self.profile_combo = QComboBox()
        self.profile_combo.setMinimumContentsLength(15)
        self.profile_combo.setSizeAdjustPolicy(
            QComboBox.SizeAdjustPolicy.AdjustToMinimumContentsLengthWithIcon
        )
        self.profile_combo.currentIndexChanged.connect(self._select_profile)
        toolbar.addWidget(self.profile_combo, 1)
        open_button = QPushButton("打开")
        open_button.setShortcut("Ctrl+O")
        open_button.clicked.connect(self.open_strategy)
        toolbar.addWidget(open_button)
        self.export_button = QPushButton("导出 CSV")
        self.export_button.setShortcut("Ctrl+E")
        self.export_button.clicked.connect(self.export_csv)
        toolbar.addWidget(self.export_button)
        panel.addLayout(toolbar)
        self.rules_label = label("", "Section", True)
        self.rules_label.hide()  # The full rule label is already in the profile selector.
        self.metadata_label = label("", "Muted", True)
        self.metadata_label.setSizePolicy(QSizePolicy.Policy.Ignored, QSizePolicy.Policy.Preferred)
        panel.addWidget(self.metadata_label)
        scenario = QHBoxLayout()
        self.position_combo = QComboBox()
        self.position_combo.currentIndexChanged.connect(self._filter_scenarios)
        scenario.addWidget(self.position_combo)
        self.scene_combo = QComboBox()
        self.scene_combo.setMinimumContentsLength(12)
        self.scene_combo.setSizeAdjustPolicy(QComboBox.SizeAdjustPolicy.AdjustToMinimumContentsLengthWithIcon)
        self.scene_combo.currentIndexChanged.connect(self._select_scene)
        scenario.addWidget(self.scene_combo, 1)
        panel.addLayout(scenario)
        compare = QHBoxLayout()
        self.compare_combo = QComboBox()
        self.compare_combo.setMinimumContentsLength(12)
        self.compare_combo.setSizeAdjustPolicy(
            QComboBox.SizeAdjustPolicy.AdjustToMinimumContentsLengthWithIcon
        )
        self.compare_combo.currentIndexChanged.connect(self._select_comparison)
        compare.addWidget(self.compare_combo, 1)
        self.view_combo = QComboBox()
        for title, value in (
            ("行动频率", "frequency"),
            ("行动 EV 差值", "ev"),
            ("判断确定性", "confidence"),
            ("频率差异", "difference"),
        ):
            self.view_combo.addItem(title, value)
        self.view_combo.currentIndexChanged.connect(self._update_view)
        compare.addWidget(self.view_combo)
        self.mixed_only = QCheckBox("仅混合")
        self.mixed_only.toggled.connect(self._update_view)
        compare.addWidget(self.mixed_only)
        panel.addLayout(compare)
        metrics = QHBoxLayout()
        self.range_metric = label("—", "Metric")
        metrics.addWidget(self.range_metric)
        metrics.addWidget(label("组合加权行动频率", "Muted"))
        metrics.addStretch()
        self.legend = label("青绿：行动   深蓝：弃牌", "Muted")
        metrics.addWidget(self.legend)
        panel.addLayout(metrics)
        analysis = QHBoxLayout()
        analysis.setSpacing(12)
        self.grid = RangeGridWidget()
        self.grid.handSelected.connect(self._show_hand)
        analysis.addWidget(self.grid, 1)
        self.range_grids = [self.grid]
        self.inspector = QFrame()
        self.inspector.setObjectName("Inspector")
        self.inspector.setMinimumWidth(0)
        detail = QVBoxLayout(self.inspector)
        detail.setContentsMargins(12, 12, 12, 12)
        detail.setSpacing(10)
        detail.addWidget(label("手牌详情", "Section"))
        self.hand_input = QLineEdit()
        self.hand_input.setPlaceholderText("输入 AA / AKs / T9o，回车")
        self.hand_input.returnPressed.connect(self._query_hand)
        detail.addWidget(self.hand_input)
        self.hand_label = label("点击手牌查看", "Section", True)
        detail.addWidget(self.hand_label)
        self.hand_detail = label("", "Muted", True)
        self.hand_detail.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        detail.addWidget(self.hand_detail)
        self.comparison_detail = label("", "Muted", True)
        detail.addWidget(self.comparison_detail)
        self.focus_samples = QComboBox()
        for n in (100000, 1000000, 10000000):
            self.focus_samples.addItem(f"精算 {n:,} 次", n)
        self.focus_samples.setCurrentIndex(1)
        detail.addWidget(self.focus_samples)
        self.focus_button = QPushButton("独立精算此手")
        self.focus_button.clicked.connect(self.start_hand_audit)
        detail.addWidget(self.focus_button)
        self.focus_detail = label("使用新样本重估此场景的 EV 与误差。", "Muted", True)
        detail.addWidget(self.focus_detail)
        detail.addStretch()
        self.inspector_scroll = QScrollArea()
        self.inspector_scroll.setWidgetResizable(True)
        self.inspector_scroll.setFixedWidth(262)
        self.inspector_scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.inspector_scroll.setWidget(self.inspector)
        analysis.addWidget(self.inspector_scroll)
        panel.addLayout(analysis, 1)
        self.comparison_note = label("上三角同花，下三角非同花。方向键切换手牌。", "Muted", True)
        panel.addWidget(self.comparison_note)
        history = QFrame()
        history.setObjectName("Card")
        history_layout = QVBoxLayout(history)
        actions = QHBoxLayout()
        actions.addWidget(label("训练记录", "Section"))
        actions.addStretch()
        for title, callback in (
            ("载入结果", self.open_job_result),
            ("继续训练", self.resume_job),
            ("打开文件夹", self.open_job_folder),
        ):
            button = QPushButton(title)
            button.clicked.connect(callback)
            actions.addWidget(button)
        history_layout.addLayout(actions)
        self.job_tree = QTreeWidget()
        self.job_tree.setHeaderLabels(["开始时间", "状态", "策略文件"])
        self.job_tree.setRootIsDecorated(False)
        self.job_tree.setAlternatingRowColors(True)
        self.job_tree.itemSelectionChanged.connect(self.show_job_log)
        self.job_tree.itemDoubleClicked.connect(lambda *_: self.open_job_result())
        self.job_log = QPlainTextEdit()
        self.job_log.setReadOnly(True)
        self.job_log.setMaximumBlockCount(2000)
        self.job_log.setPlaceholderText("选择记录查看完整命令、引擎指纹和运行日志。")
        splitter = QSplitter(Qt.Orientation.Vertical)
        splitter.addWidget(self.job_tree)
        splitter.addWidget(self.job_log)
        history_layout.addWidget(splitter, 1)
        self.tabs.addTab(history, "训练与日志")
        about = QPlainTextEdit()
        about.setReadOnly(True)
        about.setPlainText(
            f"Texas Hold’em AoF Studio {VERSION}\n\n"
            "适用范围\n翻前全下 / 弃牌，2–4 人等筹码，无前注。总筹码包含盲注。底池只抽水一次，未跟注部分返还；支持比例抽水或固定 BB 抽水，两种模式均可设置无人跟注不抽水。固定抽水不超过实际底池，比例模式另可设置封顶。\n\n"
            "读懂结果\nEV 以发盲注前的筹码为基准。混合频率是采样策略。逐手置信区间跨过零时，现有样本不能明确区分行动优劣。单手精算采用新随机样本，区间只针对该次固定预算评估。\n\n"
            "训练算法\n线性 CFR 为已验证基线。DCFR 与 2026 年 HS-DCFR 是可选实验实现；四人及抽水博弈没有沿用双人零和的纳什收敛保证。检查点保存完整训练状态，HS 调度周期在首次训练时固定，延长后保持末端参数。\n\n"
            "数据与隐私\n策略、训练命令、日志、检查点均保存在本机，无遥测。界面参数只作用于下一次训练。训练记录保存引擎和结果的 SHA-256，载入记录时检查结果是否变化。\n\n"
            "开源组件\nOMPEval（ISC），Qt / PySide6 Essentials（LGPLv3 / GPLv3 / 商业许可），PyInstaller（GPL with bootloader exception）。随发行包提供许可证、依赖清单与构建源代码。\n\n"
            "研究与发布资料\ndocs/research.txt、docs/RELEASE_NOTES.txt、THIRD_PARTY_NOTICES.txt。\n"
        )
        self.tabs.addTab(about, "说明与来源")
        self.refresh_jobs()
        self.status_label = label("", "Status", True)
        layout.addWidget(self.status_label)
        try:
            theme = int(QSettings().value("theme", 0)) if not os.environ.get("PYTEST_CURRENT_TEST") else 0
        except (TypeError, ValueError):
            theme = 0
        self.theme_combo.setCurrentIndex(1 if theme == 1 else 0)
        self._apply_theme(self.theme_combo.currentIndex())

    @staticmethod
    def _double(low, high, value, suffix, decimals=2):
        box = QDoubleSpinBox()
        box.setDecimals(decimals)
        box.setRange(low, high)
        box.setValue(value)
        box.setSuffix(suffix)
        return box

    @staticmethod
    def _integer(low, high, value, step=1):
        box = QSpinBox()
        box.setRange(low, high)
        box.setSingleStep(step)
        box.setValue(value)
        return box

    def _set_mode(self, mode: Mode):
        self.mode = mode
        self.mode_buttons[mode].setChecked(True)
        if self.process is None:
            self.players_input.setCurrentIndex(mode.value - 2)
        self.profile_combo.blockSignals(True)
        self.profile_combo.clear()
        docs = [d for d in self.documents if d.players == mode.value]
        docs.sort(
            key=lambda d: (
                d.training_method == 2,
                not d.engine.startswith("旧版"),
                d.params.rake_rate == 0.03,
                d.iterations,
            ),
            reverse=True,
        )
        for doc in docs:
            self.profile_combo.addItem(f"{doc.label()} · {doc.source.name}", doc)
        self.profile_combo.blockSignals(False)
        self._select_profile(0 if docs else -1)

    def _select_profile(self, index: int):
        self.current = self.profile_combo.itemData(index) if index >= 0 else None
        self.export_button.setEnabled(self.current is not None)
        self.position_combo.blockSignals(True)
        self.position_combo.clear()
        self.position_combo.addItem("全部位置", -1)
        if self.current:
            doc = self.current
            for seat, pos in enumerate(positions(doc.players)):
                self.position_combo.addItem(pos, seat)
            self.rules_label.setText(doc.label())
            ev = "   ".join(f"{pos} {value:+.3f}" for pos, value in zip(positions(doc.players), doc.ev))
            if doc.evaluation_samples:
                text = f"{doc.engine} · 训练 {doc.iterations:,} 次采样 · 全局评估 {doc.evaluation_samples:,} 手 · 种子 {doc.seed}\n"
                text += f"EV（BB/手）：{ev}   |   平均抽水 {doc.expected_rake:.4f}   |   采样偏离和 {doc.metric:.4f}"
                if doc.audit_samples:
                    text += f"\n逐手精度评估 {doc.audit_samples:,} 次 · 偏离和 {doc.confidence:.0%} 同时置信上界 {doc.deviation_upper:.4f} BB/手"
            else:
                text = f"{doc.engine} · {doc.iterations:,} 次迭代 · 无抽水历史策略\nEV（BB/手）：{ev}"
            self.metadata_label.setText(text)
            self.metadata_label.setToolTip(str(doc.source))
        else:
            self.rules_label.setText(f"尚无 {self.mode.value} 人策略")
            self.metadata_label.setText("在左侧设置人数、筹码和抽水后开始训练，或打开已有策略文件。")
        self.position_combo.blockSignals(False)
        self._populate_comparison()
        self._filter_scenarios()

    def _filter_scenarios(self):
        self.scene_combo.blockSignals(True)
        self.scene_combo.clear()
        seat = self.position_combo.currentData()
        if self.current:
            for node in self.current.nodes:
                if seat in (-1, None, node.seat):
                    self.scene_combo.addItem(node.title(self.current.players), node)
        self.scene_combo.blockSignals(False)
        self._select_scene()

    def _select_scene(self):
        record = self.scene_combo.currentData()
        self.grid.set_record(record)
        self.range_metric.setText(f"{record.weighted_frequency() * 100:.1f}%" if record else "—")
        self._update_view()

    def _query_hand(self):
        try:
            hand = parse_hand_input(self.hand_input.text())
        except HandParseError:
            self.status_label.setText("手牌格式不正确：对子输入 AA，非对子输入 AKs（同花）或 AKo（非同花）。")
            return
        self.status_label.setText("")
        self.grid.select_hand(hand.index())

    def _show_hand(self, index: int):
        node = self.grid.record
        if not node:
            self.hand_label.setText("等待策略")
            self.hand_detail.setText("")
            return
        self._show_focus_result(index)
        hand = HandClass.from_index(index).label()
        freq = node.frequency[index]
        self.hand_label.setText(f"{hand}   行动 {freq * 100:.2f}%   /   弃牌 {(1 - freq) * 100:.2f}%")
        count = node.effective_samples[index] if node.effective_samples is not None else None
        if count == 0:
            self.hand_detail.setText("该手牌在此行动场景没有有效评估样本，EV 暂不可用。")
            return
        a, f = node.action_ev[index], node.fold_ev[index]
        text = f"行动 EV {a:+.4f} BB   ·   弃牌 EV {f:+.4f} BB   ·   差值 {a - f:+.4f} BB"
        if count is not None:
            text += f"   ·   有效样本约 {count:,.0f}"
            if count < 100:
                text += "（样本较少）"
        if self.current.audit_samples and node.action_ev_std_error is not None:
            lo, hi = node.advantage_lower[index], node.advantage_upper[index]
            text += f"\nEV 标准误 {node.action_ev_std_error[index]:.4f} BB · 差值的 {self.current.confidence:.0%} 同时置信区间 [{lo:+.4f}, {hi:+.4f}] BB"
            if lo <= 0 <= hi:
                text += " · 尚不能可靠区分全下与弃牌，频率仅供参考"
            else:
                text += " · 当前评估支持" + ("全下 / 跟注" if lo > 0 else "弃牌")
        elif self.current.evaluation_samples:
            text += "\n此旧结果没有逐手误差区间，建议使用新引擎重新训练。"
        self.hand_detail.setText(text.replace("   ·   ", "\n").replace(" · ", "\n"))
        self._show_comparison(index)

    def add_strategy(self, path: Path):
        doc = load_strategy(path)
        self.documents = [d for d in self.documents if d.source.resolve() != path.resolve()]
        self.documents.append(doc)
        self._set_mode(Mode(doc.players))
        for i in range(self.profile_combo.count()):
            if self.profile_combo.itemData(i) is doc:
                self.profile_combo.setCurrentIndex(i)
                break
        return doc

    def open_strategy(self):
        name, _ = QFileDialog.getOpenFileName(
            self, "打开策略", str(runtime_base_dir()), "策略文件 (*.bin);;所有文件 (*)"
        )
        if name:
            try:
                self.add_strategy(Path(name))
                self.status_label.setText(f"已载入：{name}")
            except (OSError, ValueError, struct.error) as e:
                self.status_label.setText(f"无法载入策略：{e}")

    def export_csv(self):
        if not self.current:
            return
        name, _ = QFileDialog.getSaveFileName(
            self, "导出全部行动场景", f"aof_{self.current.players}p.csv", "CSV (*.csv)"
        )
        if name:
            try:
                self.current.export_csv(Path(name))
                self.status_label.setText(f"已导出：{name}")
            except OSError as e:
                self.status_label.setText(f"导出失败：{e}")

    def _update_rake_mode(self):
        fixed = self.rake_mode_input.currentData() == "fixed"
        self.rules_form.setRowVisible(self.rake_input, not fixed)
        self.rules_form.setRowVisible(self.cap_input, not fixed)
        self.rules_form.setRowVisible(self.fixed_rake_input, fixed)

    def training_args(self, output: Path) -> list[str]:
        fixed = self.rake_mode_input.currentData() == "fixed"
        p = GameParams(
            self.sb_input.value(),
            self.bb_input.value(),
            self.stack_input.value(),
            0.0 if fixed else self.rake_input.value() / 100,
            0.0 if fixed else self.cap_input.value(),
            self.no_flop.isChecked(),
            self.rake_mode_input.currentData(),
            self.fixed_rake_input.value() if fixed else 0.0,
        )
        args = [
            "--engine",
            "stratified",
            "--discounting",
            self.algorithm_input.currentData(),
            "--batch-samples",
            str(self.batch_input.value()),
            "--players",
            str(self.players_input.currentData()),
            "--stack",
            str(p.stack),
            "--sb-blind",
            str(p.sb_blind),
            "--bb-blind",
            str(p.bb_blind),
            "--iters",
            str(self.samples_input.value()),
            "--eval-samples",
            str(self.eval_input.value()),
            "--audit-samples",
            str(self.audit_input.value()),
            "--seed",
            str(self.seed_input.value()),
            "--threads",
            str(self.threads_input.value()),
            "--log-every",
            str(max(2048, self.samples_input.value() // 100)),
            "--strategy",
            str(output),
            "--checkpoint",
            str(output.with_suffix(".checkpoint")),
        ]
        if fixed:
            args += ["--rake-fixed", str(p.rake_fixed)]
        else:
            args += ["--rake-percent", str(p.rake_rate * 100), "--rake-cap", str(p.rake_cap)]
        if not p.no_flop_no_drop:
            args.append("--rake-uncontested")
        return args

    def start_training(self):
        if self.process is not None:
            return
        trainer = trainer_path()
        if trainer is None:
            self.status_label.setText(
                "未找到训练引擎。请运行 build_windows.bat 编译，或使用包含引擎的新版便携包。"
            )
            return
        try:
            directory = user_strategy_dir()
            stamp = datetime.now().astimezone().strftime("%Y%m%d_%H%M%S_%f")
            output = directory / f"strategy_{self.players_input.currentData()}p_{stamp}.bin"
            args = self.training_args(output)
            directory.mkdir(parents=True, exist_ok=True)
        except (ValueError, OSError) as e:
            self.status_label.setText(f"无法开始训练：{e}")
            return
        self._launch_training(trainer, args, output)

    def resume_training(self, checkpoint=None):
        if self.process is not None:
            return
        if not checkpoint:
            name, _ = QFileDialog.getOpenFileName(
                self,
                "选择训练检查点（追加当前采样预算）",
                str(user_strategy_dir()),
                "训练检查点 (*.checkpoint)",
            )
            if not name:
                return
            checkpoint = Path(name)
        try:
            if Path(checkpoint).stat().st_size > 8_000_000:
                raise ValueError("检查点文件过大")
            with Path(checkpoint).open(encoding="ascii") as f:
                magic = f.readline().strip()
                if magic not in ("AOFCHK2", "AOFCHK3", "AOFCHK4"):
                    raise ValueError("不是可恢复的训练检查点")
                rules = f.readline().split()
                state = f.readline().split()
                optimizer = f.readline().split() if magic != "AOFCHK2" else ["0", "0"]
            if len(rules) != (9 if magic == "AOFCHK4" else 7) or len(state) != 7:
                raise ValueError("检查点头部损坏")
            players = int(rules[0])
            mode, fixed_amount = (int(rules[7]), float(rules[8])) if magic == "AOFCHK4" else (0, 0.0)
            if mode not in (0, 1) or int(rules[6]) not in (0, 1):
                raise ValueError("检查点抽水模式无效")
            params = GameParams(
                *map(float, rules[1:6]), bool(int(rules[6])), ("percentage", "fixed")[mode], fixed_amount
            )
            seed, linear, batch, lanes, steps = map(int, state[:5])
            if (
                players not in (2, 3, 4)
                or linear not in (0, 1)
                or lanes != 32
                or not 1 <= batch <= 1024
                or steps < 0
            ):
                raise ValueError("检查点参数无效")
            discount, horizon = map(int, optimizer)
            if discount not in (0, 1, 2) or (discount == 2 and horizon <= 0):
                raise ValueError("检查点算法无效")
            self.algorithm_input.setCurrentIndex(discount)
            self.batch_input.setValue(batch)
            added = self.samples_input.value()
            completed = steps * batch * 169 * (players * 2 ** (players - 1) - 1)
            directory = user_strategy_dir()
            directory.mkdir(parents=True, exist_ok=True)
            stamp = datetime.now().astimezone().strftime("%Y%m%d_%H%M%S_%f")
            output = directory / f"strategy_{players}p_{stamp}.bin"
            self.players_input.setCurrentIndex(players - 2)
            self.rake_mode_input.setCurrentIndex(mode)
            for widget, value in (
                (self.stack_input, params.stack),
                (self.sb_input, params.sb_blind),
                (self.bb_input, params.bb_blind),
                (self.rake_input, params.rake_rate * 100),
                (self.cap_input, params.rake_cap),
                (self.fixed_rake_input, params.rake_fixed),
                (self.seed_input, seed),
            ):
                widget.setValue(value)
            self.no_flop.setChecked(params.no_flop_no_drop)
            args = self.training_args(output)
            replacements = {
                "--players": players,
                "--stack": params.stack,
                "--sb-blind": params.sb_blind,
                "--bb-blind": params.bb_blind,
                "--seed": seed,
                "--iters": completed + added,
            }
            if params.rake_mode == "fixed":
                replacements["--rake-fixed"] = params.rake_fixed
            else:
                replacements.update({"--rake-percent": params.rake_rate * 100, "--rake-cap": params.rake_cap})
            for option, value in replacements.items():
                args[args.index(option) + 1] = str(value)
            args += [
                "--resume",
                str(checkpoint),
                "--weighting",
                "linear" if linear else "uniform",
                "--batch-samples",
                str(batch),
            ]
            trainer = trainer_path()
            if trainer is None:
                raise ValueError("未找到训练引擎")
        except (OSError, ValueError, OverflowError) as e:
            self.status_label.setText(f"无法恢复训练：{e}")
            return
        self._launch_training(trainer, args, output)
        self.status_label.setText(
            f"沿用检查点规则，从 {completed:,} 次采样继续，追加 {added:,} 次；旧检查点保留。"
        )

    def _launch_training(self, trainer, args, output):
        try:
            self.job_id = self.workspace.start(output, [str(trainer), *args], trainer)
        except (OSError, sqlite3.Error) as error:
            self.status_label.setText(f"无法保存训练记录：{error}")
            return
        self.refresh_jobs()
        self.training_output = output
        self.cancelled = False
        self.log_tail = ""
        self.process_buffer = ""
        self.process = QProcess(self)
        self.process.setProcessChannelMode(QProcess.ProcessChannelMode.MergedChannels)
        self.process.setProgram(str(trainer))
        self.process.setArguments(args)
        self.process.setWorkingDirectory(str(runtime_base_dir()))
        self.process.readyReadStandardOutput.connect(self._read_progress)
        self.process.finished.connect(self._training_finished)
        self.process.errorOccurred.connect(self._process_error)
        self.train_button.setEnabled(False)
        self.resume_button.setEnabled(False)
        self.config_widget.setEnabled(False)
        self.cancel_button.setEnabled(True)
        self.progress.setValue(0)
        self.training_label.setText("正在训练，完成后自动载入结果。")
        self.status_label.setText("训练中的新参数仅用于新结果；当前范围仍对应上方显示的策略规则。")
        self.process.start()

    def _read_progress(self):
        if self.process is None:
            return
        text = bytes(self.process.readAllStandardOutput()).decode("utf-8", errors="replace")
        if self.job_id:
            try:
                self.workspace.append_log(self.job_id, text)
            except OSError as error:
                self.status_label.setText(f"日志写入失败：{error}")
        if self._selected_job() and self._selected_job()["id"] == self.job_id:
            self.show_job_log()
        self.log_tail = (self.log_tail + text)[-6000:]
        self.process_buffer += text
        lines = self.process_buffer.split("\n")
        self.process_buffer = lines.pop()
        for line in lines:
            match = re.search(
                r"\[(sampled-cfr|stratified-cfr|evaluate|audit)\] completed=(\d+) total=(\d+)", line
            )
            if match:
                stage, done, total = match.groups()
                fraction = int(done) / max(1, int(total))
                self.progress.setValue(
                    round(
                        90 + 10 * fraction
                        if stage == "audit"
                        else 80 + 10 * fraction
                        if stage == "evaluate"
                        else 80 * fraction
                    )
                )
                phase = "逐手精度评估" if stage == "audit" else "全局评估" if stage == "evaluate" else "训练"
                self.training_label.setText(f"{phase}：{int(done):,} / {int(total):,} 次采样")

    def _process_error(self, error):
        if error == QProcess.ProcessError.FailedToStart and self.process:
            self.status_label.setText("训练引擎启动失败：" + self.process.errorString())
            if self.job_id:
                self.workspace.finish(self.job_id, "failed", self.process.errorString())
                self.refresh_jobs()
            self._reset_process()

    def _reset_process(self):
        process = self.process
        self.process = None
        if process is not None:
            process.deleteLater()
        self.train_button.setEnabled(True)
        self.resume_button.setEnabled(True)
        self.config_widget.setEnabled(True)
        self.cancel_button.setEnabled(False)

    def _training_finished(self, code, status):
        self._read_progress()
        success = code == 0 and status == QProcess.ExitStatus.NormalExit and not self.cancelled
        self._reset_process()
        if success and self.training_output is not None:
            try:
                self.add_strategy(self.training_output)
                self.progress.setValue(100)
                self.training_label.setText("训练与独立评估完成，已载入新策略。")
                self.status_label.setText(f"已保存：{self.training_output}")
            except (OSError, ValueError, struct.error) as e:
                self.status_label.setText(f"训练结束，但结果无法载入：{e}")
        elif self.cancelled:
            self.training_label.setText("训练已取消；已写入的检查点可用于继续训练。")
            self.status_label.setText("当前已载入策略仍可查看。")
            if self.training_output:
                try:
                    self.training_output.unlink(missing_ok=True)
                except OSError as e:
                    self.status_label.setText(f"训练已取消，临时文件无法移除：{e}")
        else:
            self.training_label.setText("训练失败。")
            self.status_label.setText(self.log_tail[-1500:] or f"训练进程异常退出：{code}")

        if self.job_id:
            self.workspace.finish(
                self.job_id,
                "succeeded"
                if success and self.current and self.current.source == self.training_output
                else "cancelled"
                if self.cancelled
                else "failed",
                self.training_label.text(),
                self.training_output if success else None,
            )
            self.refresh_jobs()

    def cancel_training(self):
        if self.process:
            self.cancelled = True
            self.cancel_button.setEnabled(False)
            self.process.kill()

    def closeEvent(self, event):
        if self.audit_process:
            self.audit_process.kill()
            self.audit_process.waitForFinished(2000)
        if self.process:
            self.cancel_training()
            self.process.waitForFinished(2000)
        event.accept()


def main() -> int:
    app = QApplication(sys.argv)
    app.setApplicationName("TexasHoldemAoFGTO")
    app.setOrganizationName("AoF Research")
    app.setStyle("Fusion")
    app.setFont(QFont("Microsoft YaHei UI", 10))
    base = runtime_base_dir()
    logger = logging.getLogger("aofstudio")
    logger.setLevel(logging.INFO)
    try:
        log_dir = user_strategy_dir()
        log_dir.mkdir(parents=True, exist_ok=True)
        handler = RotatingFileHandler(
            log_dir / "application.log", maxBytes=1000000, backupCount=2, encoding="utf-8"
        )
        handler.setFormatter(logging.Formatter("%(asctime)s %(levelname)s %(message)s"))
        logger.addHandler(handler)
    except OSError:
        logger.addHandler(logging.NullHandler())
    windows = []

    def exception_hook(kind, value, trace):
        logger.error("Unhandled application error", exc_info=(kind, value, trace))
        if windows:
            windows[0].status_label.setText("发生异常，详细信息已写入 application.log。")

    sys.excepthook = exception_hook
    logger.info("AoF Studio %s started", VERSION)
    extra = [user_strategy_dir()]
    if getattr(sys, "frozen", False):
        extra.append(Path(sys.executable).resolve().parent / "data")
    bundle = load_bundle(base / "data", tuple(extra))
    if os.environ.get("AOF2_GUI_TEST_REQUIRE_DATA") and not {2, 3, 4}.issubset(
        {d.players for d in bundle.documents}
    ):
        print("Missing bundled player modes", file=sys.stderr)
        return 2
    win = AppWindow(bundle)
    windows.append(win)
    if os.environ.get("AOF2_GUI_SELF_TEST_REPORT"):
        win.setAttribute(Qt.WidgetAttribute.WA_DontShowOnScreen, True)
    win.show()
    if os.environ.get("AOF2_GUI_SELF_TEST_REPORT"):
        from portable_smoke import run_smoke

        run_smoke(app, win, Path(os.environ["AOF2_GUI_SELF_TEST_REPORT"]))
    if os.environ.get("AOF2_GUI_TEST_EXIT_MS"):
        QTimer.singleShot(max(0, int(os.environ["AOF2_GUI_TEST_EXIT_MS"])), app.quit)
    return app.exec()
