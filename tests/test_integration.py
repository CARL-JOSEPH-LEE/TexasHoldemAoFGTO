import csv
import math
import os
import struct
import subprocess
from pathlib import Path

import pytest
from strategy_data import GameParams, HandClass, load_strategy, parse_hand_input
from workbench import load_bundle

ROOT = Path(__file__).resolve().parents[1]
TRAINER = ROOT / "build" / ("aof2_train.exe" if os.name == "nt" else "aof2_train")
EVALUATOR = TRAINER.with_name("aof2_eval.exe" if os.name == "nt" else "aof2_eval")


def run(*args, ok=True):
    result = subprocess.run(
        [str(TRAINER), *map(str, args)],
        cwd=ROOT,
        capture_output=True,
        text=True,
        encoding="utf-8",
        timeout=60,
    )
    assert (result.returncode == 0) == ok, result.stdout + result.stderr
    return result


@pytest.fixture(scope="module")
def generated(tmp_path_factory):
    directory = tmp_path_factory.mktemp("strategies")
    results = {}
    for players in (2, 3, 4):
        for rate in (0, 3):
            path = directory / f"strategy_{players}p_{rate}.bin"
            run(
                "--players",
                players,
                "--iters",
                100000,
                "--eval-samples",
                50000,
                "--rake-percent",
                rate,
                "--rake-cap",
                0.5,
                "--strategy",
                path,
                "--log-every",
                0,
            )
            results[players, rate] = path
    return results


def test_hand_classes_and_grid_orientation():
    assert sum(HandClass.from_index(i).num_combos() for i in range(169)) == 1326
    for i in range(169):
        hand = HandClass.from_index(i)
        assert hand.index() == i
        assert parse_hand_input(hand.label()) == hand
    from range_grid import RangeGridWidget

    assert RangeGridWidget.hand_at(0, 1).label() == "AKs"
    assert RangeGridWidget.hand_at(1, 0).label() == "AKo"


def test_all_player_counts_and_rake_metadata(generated):
    for (players, rake), path in generated.items():
        doc = load_strategy(path)
        assert doc.players == players
        assert len(doc.nodes) == (1 << players) - 2
        assert doc.params.rake_rate == rake / 100
        assert doc.params.rake_cap == 0.5
        assert doc.evaluation_samples == 50000
        assert abs(sum(doc.ev) + doc.expected_rake) < 1e-8
        assert 0 <= doc.expected_rake <= 0.5
        if rake == 0:
            assert doc.expected_rake == 0
        for node in doc.nodes:
            assert all(0 <= f <= 1 for f in node.frequency)
            assert all(math.isfinite(v) for v in node.action_ev)
    assert (
        load_strategy(generated[4, 0]).nodes[0].frequency != load_strategy(generated[4, 3]).nodes[0].frequency
    )


def test_csv_and_evaluation(generated, tmp_path):
    path = generated[4, 3]
    doc = load_strategy(path)
    csv_path = tmp_path / "strategy.csv"
    doc.export_csv(csv_path)
    with csv_path.open(encoding="utf-8-sig", newline="") as f:
        rows = list(csv.DictReader(f))
    assert len(rows) == 14 * 169
    assert {row["position"] for row in rows} == {"CO", "BTN", "SB", "BB"}
    assert {row["rake_percent"] for row in rows} == {"3.0"}
    result = subprocess.run(
        [str(EVALUATOR), "--strategy", str(path), "--samples", "10000", "--csv", str(tmp_path / "eval.csv")],
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert result.returncode == 0, result.stderr
    assert "Expected rake=" in result.stdout
    assert "rake=3%" in result.stdout


@pytest.mark.parametrize(
    "args",
    [
        ("--players", "5"),
        ("--players", "4x"),
        ("--iters", "-1"),
        ("--iters", "0"),
        ("--iters", "100x"),
        ("--stack", "nan"),
        ("--stack", "inf"),
        ("--stack", "1"),
        ("--sb-blind", "0"),
        ("--rake-percent", "-1"),
        ("--rake-percent", "101"),
        ("--rake-cap", "-1"),
        ("--eval-samples", "1"),
        ("--threads", "4294967296"),
        ("--engine", "unknown"),
        ("--weighting", "dcfr"),
        ("--batch-samples", "0"),
        ("--audit-samples", "1"),
        ("--confidence", "1"),
        ("--checkpoint-every", "0"),
        ("--engine", "table", "--players", "4"),
        ("--engine", "table", "--rake-percent", "3"),
        ("--strategy",),
    ],
)
def test_invalid_cli_fails_before_training(args):
    run(*args, ok=False)


def test_help_and_output_collision(tmp_path):
    assert "--rake-percent" in run("--help").stderr
    run("--strategy", tmp_path / "same", "--csv", tmp_path / "same", ok=False)


def test_file_validation_and_discovery(generated, tmp_path):
    original = generated[4, 3].read_bytes()
    for damaged in (original[:11], original[:-1], original + b"x"):
        path = tmp_path / "strategy_bad.bin"
        path.write_bytes(damaged)
        with pytest.raises((ValueError, struct.error)):
            load_strategy(path)
    damaged = bytearray(original)
    # v2 header 272 bytes; node identity 8 bytes; first frequency follows.
    struct.pack_into("<d", damaged, 280, float("nan"))
    path.write_bytes(damaged)
    with pytest.raises(ValueError):
        load_strategy(path)
    (tmp_path / "strategy_good.bin").write_bytes(original)
    bundle = load_bundle(tmp_path)
    assert len(bundle.documents) == 1
    assert len(bundle.errors) == 1
    assert bundle.s2p is None  # four-player data must never be misidentified as 2P


def test_legacy_strategy_compatibility():
    for filename, players in (("strategy_10bb.bin", 2), ("strategy_3p_10bb.bin", 3)):
        doc = load_strategy(ROOT / "data" / filename)
        assert doc.players == players
        assert doc.params.rake_rate == 0
        assert len(doc.nodes) == (1 << players) - 2


def test_python_rule_validation():
    for args in ((0, 1, 10), (2, 1, 10), (0.5, 1, 1), (0.5, 1, float("nan")), (0.5, 1, 10, 1.1)):
        with pytest.raises(ValueError):
            GameParams(*args)


def test_unicode_and_spaces_in_paths(tmp_path):
    path = tmp_path / "中文 目录" / "四人策略.bin"
    run("--players", 4, "--iters", 1000, "--eval-samples", 1000, "--strategy", path)
    assert load_strategy(path).players == 4
    csv_path = path.with_suffix(".csv")
    result = subprocess.run(
        [str(EVALUATOR), "--strategy", str(path), "--samples", "1000", "--csv", str(csv_path)],
        capture_output=True,
        timeout=30,
    )
    assert result.returncode == 0, result.stderr
    assert csv_path.exists()


def test_precision_metadata_and_csv_agree(generated, tmp_path):
    doc = load_strategy(generated[4, 3])
    assert doc.training_method == 2 and doc.training_sweeps > 0
    assert doc.audit_samples >= 50000 and doc.confidence == 0.99
    assert doc.deviation_upper >= doc.metric
    path = tmp_path / "precision.csv"
    doc.export_csv(path)
    with path.open(encoding="utf-8-sig", newline="") as f:
        rows = list(csv.DictReader(f))
    for node, group in zip(doc.nodes, [rows[i : i + 169] for i in range(0, len(rows), 169)]):
        for h, row in enumerate(group):
            if node.effective_samples[h] > 0:
                assert float(row["advantage_lower"]) == node.advantage_lower[h]
                assert float(row["action_ev_std_error"]) == node.action_ev_std_error[h]


def test_resume_cli_exact_and_collision(tmp_path):
    checkpoint = tmp_path / "中文 检查点.checkpoint"
    partial, resumed, full = (tmp_path / name for name in ("partial.bin", "resumed.bin", "full.bin"))
    common = [
        "--players",
        4,
        "--rake-percent",
        3,
        "--eval-samples",
        10000,
        "--audit-samples",
        10000,
        "--log-every",
        0,
    ]
    run(*common, "--iters", 30000, "--checkpoint", checkpoint, "--strategy", partial)
    run(*common, "--iters", 100000, "--resume", checkpoint, "--strategy", resumed)
    run(*common, "--iters", 100000, "--strategy", full)
    assert resumed.read_bytes() == full.read_bytes()
    run(*common, "--iters", 200000, "--resume", checkpoint, "--seed", 7, "--strategy", resumed, ok=False)
    run(*common, "--checkpoint", checkpoint, "--strategy", checkpoint, ok=False)
    checkpoint.write_text("AOFCHK2\n4 .5 1 10 .03 0 1\n", encoding="ascii")
    run(*common, "--resume", checkpoint, "--strategy", resumed, ok=False)
