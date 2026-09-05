"""Analysis and journal UI, kept separate from training orchestration."""

from __future__ import annotations

import json
import os
import secrets
from pathlib import Path

from PySide6.QtCore import QProcess, QSettings, Qt, QUrl
from PySide6.QtGui import QDesktopServices
from PySide6.QtWidgets import QTreeWidgetItem
from strategy_data import HandClass
from workspace import fingerprint


class AnalysisTools:
    def _apply_theme(self, index):
        from workbench import STYLE

        css = (
            STYLE
            + """
            QTabWidget::pane { border: none; }
            QTabBar::tab { padding: 9px 20px; border: none; color: #5b7182; }
            QTabBar::tab:selected { color: #147955; border-bottom: 3px solid #147955; font-weight: 700; }
            QFrame#Inspector { background: #f4f7fa; border-radius: 8px; }
            QPlainTextEdit, QTreeWidget { background: #ffffff; color: #162c3d; border: 1px solid #d9e3e9;
                                        alternate-background-color: #f4f7fa; padding: 8px; }
            QHeaderView::section { background: #edf2f5; color: #162c3d; padding: 7px; border: none; }
            QToolTip { background: #102c3b; color: white; border: none; padding: 6px; }
        """
        )
        if index:
            for old, new in {
                "#edf2f5": "#111a24",
                "#ffffff": "#1a2634",
                "#162c3d": "#e0e9f3",
                "#f4f7fa": "#15202d",
                "#173447": "#e0e9f3",
                "#163a50": "#d4e2f1",
                "#5b7182": "#abbdd0",
                "#d9e3e9": "#2a3b4e",
                "#cbd9e2": "#3e5167",
                "#f8fafb": "#15202d",
                "#e9f4f0": "#264a46",
                "#e9eff2": "#253444",
                "#e7eff3": "#253444",
                "#16734c": "#4dd3b1",
            }.items():
                css = css.replace(old, new)
            # Brand and primary-button text must remain light in both themes.
            css += "QLabel#Brand, QPushButton#Primary, QPushButton:checked { color: #ffffff; }"
        self.setStyleSheet(css)
        if not os.environ.get("PYTEST_CURRENT_TEST"):
            QSettings().setValue("theme", index)

    def _populate_comparison(self):
        self.compare_combo.blockSignals(True)
        self.compare_combo.clear()
        self.compare_combo.addItem("对比策略：未选择", None)
        if self.current:
            for doc in self.documents:
                if doc.players == self.current.players and doc.source != self.current.source:
                    self.compare_combo.addItem(f"{doc.label()} · {doc.source.name}", doc)
        self.compare_combo.blockSignals(False)
        self.comparison = None

    def _select_comparison(self):
        self.comparison = self.compare_combo.currentData()
        if self.comparison:
            self.view_combo.setCurrentIndex(3)
        self._update_view()

    def _comparison_node(self):
        node = self.grid.record
        if self.comparison and node:
            return next(
                (n for n in self.comparison.nodes if (n.seat, n.mask) == (node.seat, node.mask)), None
            )
        return None

    def _update_view(self):
        mode = self.view_combo.currentData() or "frequency"
        self.grid.view_mode = mode
        self.grid.comparison = self._comparison_node()
        self.grid.mixed_only = self.mixed_only.isChecked()
        self.grid.update()
        self.legend.setText(
            {
                "frequency": "青绿：行动   深蓝：弃牌",
                "ev": "蓝：EV 为正   橙：EV 为负",
                "confidence": "蓝：行动占优   橙：弃牌占优   灰：未区分",
                "difference": "蓝：行动增加   橙：行动减少",
            }[mode]
        )
        note = "上三角同花，下三角非同花。方向键切换手牌。"
        if self.current and self.comparison:
            a, b = self.current, self.comparison
            note = "差异 = 当前策略 − 对比策略；单位为百分点。"
            if a.params != b.params:
                note += " 两份策略规则不同，请结合各自的筹码与抽水比较。"
        elif mode == "difference":
            note = "请选择另一份策略以显示频率差异。"
        self.comparison_note.setText(note)
        self._show_hand(self.grid.selected)

    def _show_comparison(self, index):
        node = self._comparison_node()
        text = ""
        if node and self.grid.record:
            delta = 100 * (self.grid.record.frequency[index] - node.frequency[index])
            text = f"对比频率 {node.frequency[index]:.2%}\n当前差异 {delta:+.2f} 个百分点"
        self.comparison_detail.setText(text)

    def _audit_key(self, index):
        if self.current is None or self.grid.record is None:
            return None
        node = self.grid.record
        return (self.current.content_hash, node.seat, node.mask, index)

    def _show_focus_result(self, index):
        result = self.audits.get(self._audit_key(index))
        self.focus_button.setEnabled(
            self.audit_process is None and bool(self.current and self.current.evaluation_samples)
        )
        if not result:
            self.focus_detail.setText("精算采用新样本，独立于已载入的训练与评估。")
            return
        if result["effective_samples"] <= 0:
            self.focus_detail.setText("此手牌下，该行动历史不可达；条件 EV 无定义。")
            return
        delta = result["action_ev"] - result["fold_ev"]
        self.focus_detail.setText(
            f"单手精算 {result['samples']:,} 次\nEV 差值 {delta:+.5f} BB\n"
            f"标准误 {result['std_error']:.5f} BB\n"
            f"本次 99% 区间\n[{result['lower']:+.5f}, {result['upper']:+.5f}] BB"
        )

    def start_hand_audit(self):
        from workbench import runtime_base_dir

        if self.audit_process or not self.current or not self.grid.record:
            return
        doc = self.current
        if not doc.evaluation_samples:
            self.status_label.setText("单手精算需要新版采样策略。")
            return
        name = "aof2_eval.exe" if os.name == "nt" else "aof2_eval"
        root = runtime_base_dir()
        engine = next(
            (
                p
                for p in (root / "bin" / name, root / "build" / name, root / "build" / "Release" / name)
                if p.is_file()
            ),
            None,
        )
        if engine is None:
            self.status_label.setText("缺少独立评估引擎，请重新构建或使用完整发行包。")
            return
        try:
            if fingerprint(doc.source) != doc.content_hash:
                raise ValueError("策略文件已变化，请重新打开后评估")
        except (OSError, ValueError) as error:
            self.status_label.setText(str(error))
            return
        node = doc.nodes.index(self.grid.record)
        hand = HandClass.from_index(self.grid.selected).label()
        self.audit_key = self._audit_key(self.grid.selected)
        self.audit_origin = doc.source
        self.audit_buffer = ""
        process = QProcess(self)
        self.audit_process = process
        process.setProcessChannelMode(QProcess.ProcessChannelMode.MergedChannels)
        process.setProgram(str(engine))
        process.setArguments(
            [
                "--strategy",
                str(doc.source),
                "--node",
                str(node),
                "--hand",
                hand,
                "--samples",
                str(self.focus_samples.currentData()),
                "--seed",
                str(secrets.randbits(32)),
                "--threads",
                str(self.threads_input.value()),
            ]
        )
        process.readyReadStandardOutput.connect(self._read_audit)
        process.finished.connect(self._audit_finished)
        process.errorOccurred.connect(self._audit_error)
        self.focus_button.setEnabled(False)
        self.focus_detail.setText(f"正在独立精算 {hand}…")
        process.start()

    def _read_audit(self):
        if self.audit_process:
            self.audit_buffer += bytes(self.audit_process.readAllStandardOutput()).decode(
                "utf-8", errors="replace"
            )

    def _audit_error(self, error):
        if error == QProcess.ProcessError.FailedToStart:
            self._audit_finished(2, QProcess.ExitStatus.CrashExit)

    def _audit_finished(self, code, status):
        self._read_audit()
        process, self.audit_process = self.audit_process, None
        if process:
            process.deleteLater()
        try:
            if code != 0 or status != QProcess.ExitStatus.NormalExit:
                raise ValueError(self.audit_buffer[-500:] or "独立评估进程未完成")
            result = json.loads(self.audit_buffer)
            if fingerprint(self.audit_origin) != self.audit_key[0]:
                raise ValueError("评估期间策略文件发生变化，结果未采用")
            self.audits[self.audit_key] = result
            archive = {"strategy_sha256": self.audit_key[0], "result": result}
            destination = self.workspace.directory / f"audit_{secrets.token_hex(8)}.json"
            destination.write_text(json.dumps(archive, ensure_ascii=False, indent=2), encoding="utf-8")
            self.status_label.setText(f"单手精算完成，结果与种子已保存：{destination.name}")
        except (ValueError, KeyError, OSError) as error:
            self.status_label.setText(f"精算失败：{error}")
        self._show_focus_result(self.grid.selected)

    def refresh_jobs(self):
        selected = self.job_tree.currentItem()
        old_id = selected.data(0, Qt.ItemDataRole.UserRole)["id"] if selected else self.job_id
        self.job_tree.clear()
        labels = {
            "running": "运行中",
            "succeeded": "已完成",
            "failed": "失败",
            "cancelled": "已取消",
            "interrupted": "可恢复",
        }
        for job in self.workspace.jobs():
            item = QTreeWidgetItem(
                [
                    job["created"].replace("T", " ").replace("+00:00", " UTC"),
                    labels.get(job["status"], job["status"]),
                    Path(job["output"]).name,
                ]
            )
            item.setData(0, Qt.ItemDataRole.UserRole, job)
            self.job_tree.addTopLevelItem(item)
            if job["id"] == old_id:
                self.job_tree.setCurrentItem(item)
        self.job_tree.resizeColumnToContents(0)
        self.job_tree.resizeColumnToContents(1)

    def _selected_job(self):
        item = self.job_tree.currentItem()
        return item.data(0, Qt.ItemDataRole.UserRole) if item else None

    def show_job_log(self):
        job = self._selected_job()
        if job:
            self.job_log.setPlainText(
                f"引擎 SHA-256：{job['engine_hash']}\n状态：{job['detail'] or job['status']}\n"
                f"命令参数：{job['command']}\n\n" + self.workspace.log(job["id"])
            )

    def open_job_result(self):
        job = self._selected_job()
        if not job:
            return
        try:
            path = Path(job["output"])
            if job["result_hash"] and fingerprint(path) != job["result_hash"]:
                raise ValueError("文件指纹与训练记录不一致；请检查文件，或通过“打开”明确载入新文件")
            self.add_strategy(path)
            self.tabs.setCurrentIndex(0)
        except (ValueError, OSError) as error:
            self.status_label.setText(f"无法打开结果：{error}")

    def resume_job(self):
        job = self._selected_job()
        if job:
            self.resume_training(Path(job["checkpoint"]))

    def open_job_folder(self):
        job = self._selected_job()
        folder = Path(job["output"]).parent if job else self.workspace.directory
        QDesktopServices.openUrl(QUrl.fromLocalFile(str(folder)))
