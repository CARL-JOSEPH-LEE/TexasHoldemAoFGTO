import time
from pathlib import Path

import pytest
import workbench
from PySide6.QtCore import Qt
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication
from strategy_data import HandClass, Kind
from workbench import AppWindow, Mode, StrategyBundle, load_bundle

ROOT = Path(__file__).resolve().parents[1]


@pytest.fixture(scope="module")
def app():
    application = QApplication.instance() or QApplication([])
    application.setStyle("Fusion")
    return application


@pytest.fixture
def window(app):
    win = AppWindow(load_bundle(ROOT / "data"))
    win.resize(1280, 900)
    win.show()
    app.processEvents()
    yield win
    win.close()
    app.processEvents()


def test_four_player_scenarios_and_position_filter(window, app):
    window.resize(1280, 800)
    app.processEvents()
    assert window.height() <= 800
    window._set_mode(Mode.P4)
    assert window.scene_combo.count() == 14
    assert "抽水 3%" in window.rules_label.text()
    assert window.current.params.rake_rate == 0.03
    for i in range(14):
        window.scene_combo.setCurrentIndex(i)
        assert window.grid.record is window.scene_combo.currentData()
    window.position_combo.setCurrentIndex(4)  # BB
    assert window.scene_combo.count() == 7
    assert all(window.scene_combo.itemData(i).mask > 0 for i in range(7))
    for mode, count in ((Mode.P2, 2), (Mode.P3, 6)):
        window._set_mode(mode)
        assert window.scene_combo.count() == count


def test_click_hand_and_input(window, app):
    hand = HandClass(12, 11, Kind.SUITED)
    point = window.grid.cell_rect_for(hand).center().toPoint()
    QTest.mouseClick(window.grid, Qt.MouseButton.LeftButton, pos=point)
    assert window.grid.selected == hand.index()
    assert "AKs" in window.hand_label.text()
    window.hand_input.setText("T9o")
    QTest.keyClick(window.hand_input, Qt.Key.Key_Return)
    assert "T9o" in window.hand_label.text()
    window.hand_input.setText("AAo")
    window._query_hand()
    assert "格式不正确" in window.status_label.text()


def test_settings_do_not_relabel_existing_strategy(window):
    assert window.stack_input.text() == "10.00 BB"
    before = window.rules_label.text()
    window.rake_input.setValue(7)
    window.stack_input.setValue(20)
    assert window.rules_label.text() == before
    args = window.training_args(Path("output.bin"))
    assert args[args.index("--rake-percent") + 1].startswith("7")
    assert args[args.index("--stack") + 1] == "20.0"
    window.no_flop.setChecked(False)
    assert "--rake-uncontested" in window.training_args(Path("output.bin"))


def test_empty_bundle_and_missing_engine(app, monkeypatch):
    win = AppWindow(StrategyBundle())
    assert "尚无 4 人策略" == win.rules_label.text()
    assert win.btn_mode_4p.isEnabled()
    monkeypatch.setattr(workbench, "trainer_path", lambda: None)
    win.start_training()
    assert "未找到训练引擎" in win.status_label.text()
    win.close()


def wait_for_job(window, app, timeout=20):
    start = time.monotonic()
    while window.process is not None and time.monotonic() - start < timeout:
        app.processEvents()
        QTest.qWait(10)
    assert window.process is None, "training did not finish"


def test_gui_training_and_cancel(window, app, monkeypatch, tmp_path):
    monkeypatch.setattr(workbench, "user_strategy_dir", lambda: tmp_path)
    window.players_input.setCurrentIndex(2)
    window.samples_input.setValue(20000)
    window.eval_input.setValue(10000)
    window.audit_input.setValue(10000)
    window.rake_input.setValue(3)
    window.cap_input.setValue(0.4)
    window.no_flop.setChecked(False)
    window.start_training()
    assert window.process is not None
    assert not window.train_button.isEnabled()
    wait_for_job(window, app)
    assert window.current.source == window.training_output
    assert window.current.params.rake_cap == 0.4
    assert window.current.params.no_flop_no_drop is False
    assert window.progress.value() == 100
    saved = window.current
    window.samples_input.setValue(1000000000)
    window.start_training()
    window.cancel_training()
    wait_for_job(window, app)
    assert window.current is saved
    assert window.train_button.isEnabled()
    assert not window.training_output.exists()


def test_gui_checkpoint_resume_and_uncertainty(window, app, monkeypatch, tmp_path):
    monkeypatch.setattr(workbench, "user_strategy_dir", lambda: tmp_path)
    window.seed_input.setValue(2**64 - 1)
    window.samples_input.setValue(30000)
    window.eval_input.setValue(10000)
    window.audit_input.setValue(10000)
    window.start_training()
    wait_for_job(window, app)
    old = window.current
    checkpoint = window.training_output.with_suffix(".checkpoint")
    before = checkpoint.read_bytes()
    assert old.training_method == 2
    assert "置信区间" in window.hand_detail.text()
    window.rake_input.setValue(7)
    window.resume_training(checkpoint)
    wait_for_job(window, app)
    assert window.current.iterations > old.iterations
    assert window.current.params.rake_rate == old.params.rake_rate
    assert window.current.seed == old.seed
    assert checkpoint.read_bytes() == before


def test_comparison_views_keyboard_and_visible_inspector(window, app):
    window.resize(1280, 800)
    app.processEvents()
    assert window.train_button.isVisible()
    assert window.train_button.mapTo(window, window.train_button.rect().bottomRight()).y() < window.height()
    assert window.hand_detail.mapTo(window, window.hand_detail.rect().bottomRight()).y() < window.height()
    window.compare_combo.setCurrentIndex(1)
    assert window.grid.comparison is not None and window.grid.view_mode == "difference"
    assert "百分点" in window.comparison_detail.text()
    for mode in range(4):
        window.view_combo.setCurrentIndex(mode)
        app.processEvents()
        assert not window.grid.grab().isNull()
    window.grid.select_hand(12)
    QTest.keyClick(window.grid, Qt.Key.Key_Right)
    assert "AKs" in window.hand_label.text()
    window.theme_combo.setCurrentIndex(1)
    assert "#111a24" in window.styleSheet()


def test_gui_focused_audit_and_job_history(window, app, monkeypatch, tmp_path):
    window.focus_samples.setCurrentIndex(0)
    window.start_hand_audit()
    started = time.monotonic()
    while window.audit_process is not None and time.monotonic() - started < 20:
        app.processEvents()
        QTest.qWait(10)
    assert window.audit_process is None
    assert "99%" in window.focus_detail.text()
    monkeypatch.setattr(workbench, "user_strategy_dir", lambda: tmp_path)
    window.samples_input.setValue(1000)
    window.eval_input.setValue(1000)
    window.audit_input.setValue(1000)
    window.algorithm_input.setCurrentIndex(2)
    window.start_training()
    wait_for_job(window, app)
    assert window.current.training_method == 4
    jobs = window.workspace.jobs()
    assert jobs[0]["status"] == "succeeded"
    assert jobs[0]["result_hash"] == window.current.content_hash
    window.refresh_jobs()
    window.job_tree.setCurrentItem(window.job_tree.topLevelItem(0))
    window.show_job_log()
    assert "SHA-256" in window.job_log.toPlainText()
    checkpoint = window.training_output.with_suffix(".checkpoint")
    window.resume_training(checkpoint)
    wait_for_job(window, app)
    assert window.current.training_method == 4


def test_oversized_checkpoint_is_rejected_before_read(window, tmp_path):
    path = tmp_path / "oversized.checkpoint"
    with path.open("wb") as stream:
        stream.truncate(8_000_001)
    window.resume_training(path)
    assert window.process is None
    assert "过大" in window.status_label.text()
