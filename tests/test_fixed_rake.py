import csv
import json
import os
import struct
import subprocess
import zlib
from pathlib import Path

import pytest
from strategy_data import load_strategy

ROOT = Path(__file__).resolve().parents[1]
TRAIN = ROOT / "build" / ("aof2_train.exe" if os.name == "nt" else "aof2_train")
EVAL = TRAIN.with_name("aof2_eval.exe" if os.name == "nt" else "aof2_eval")
SMALL = ["--iters", 30000, "--eval-samples", 10000, "--audit-samples", 10000, "--threads", 2]


def invoke(exe, *args, success=True):
    result = subprocess.run([str(exe), *map(str, args)], capture_output=True, timeout=30)
    assert (result.returncode == 0) == success, result.stdout + result.stderr
    return result


@pytest.mark.parametrize("players", [2, 3, 4])
@pytest.mark.parametrize("uncontested", [False, True])
def test_fixed_rake_training_evaluation_and_both_csv_exports(tmp_path, players, uncontested):
    path, native_csv, gui_csv = (tmp_path / n for n in ("strategy.bin", "native.csv", "gui.csv"))
    flags = ["--rake-uncontested"] if uncontested else []
    invoke(
        TRAIN,
        *SMALL,
        "--players",
        players,
        "--rake-fixed",
        0.5,
        *flags,
        "--strategy",
        path,
        "--csv",
        native_csv,
    )
    doc = load_strategy(path)
    assert doc.params.rake_mode == "fixed" and doc.params.rake_fixed == 0.5
    assert doc.params.rake_rate == doc.params.rake_cap == 0
    assert doc.params.no_flop_no_drop == (not uncontested)
    assert "固定抽水 0.5 BB" in doc.label()
    assert abs(sum(doc.ev) + doc.expected_rake) < 1e-9
    assert 0 <= doc.expected_rake <= 0.5
    if uncontested:
        assert doc.expected_rake == 0.5  # even the smallest 1 BB walk pot can pay .5
    doc.export_csv(gui_csv)
    for csv_path in (native_csv, gui_csv):
        with csv_path.open(encoding="utf-8-sig", newline="") as stream:
            rows = list(csv.DictReader(stream))
        assert len(rows) == ((1 << players) - 2) * 169
        assert {r["rake_mode"] for r in rows} == {"fixed"}
        assert {float(r["rake_fixed_bb"]) for r in rows} == {0.5}
    before = path.read_bytes()
    result = invoke(EVAL, "--strategy", path, "--samples", 10000, "--audit-samples", 10000)
    assert b"rake=fixed 0.5 BB" in result.stdout
    focused = json.loads(
        invoke(EVAL, "--strategy", path, "--samples", 1000, "--node", 0, "--hand", "AA").stdout
    )
    assert focused["lower"] <= focused["action_ev"] - focused["fold_ev"] <= focused["upper"]
    assert path.read_bytes() == before


@pytest.mark.parametrize(
    "extra",
    [
        ["--rake-fixed", "-1"],
        ["--rake-fixed", "nan"],
        ["--rake-fixed", "inf"],
        ["--rake-fixed", ".5", "--rake-percent", "0"],
        ["--rake", ".03", "--rake-fixed", ".5"],
        ["--rake-fixed", ".5", "--rake-cap", "0"],
        ["--engine", "table", "--rake-fixed", "0"],
    ],
)
def test_fixed_rake_invalid_and_mixed_rules_rejected(tmp_path, extra):
    invoke(TRAIN, *SMALL, "--strategy", tmp_path / "bad.bin", *extra, success=False)
    assert not (tmp_path / "bad.bin").exists()


def test_fixed_checkpoint_resume_preserves_rules_and_rng(tmp_path):
    path, resumed, full, checkpoint = (
        tmp_path / n for n in ("part.bin", "resume.bin", "full.bin", "state.checkpoint")
    )
    args = [*SMALL, "--players", 4, "--rake-fixed", 0.5]
    invoke(TRAIN, *args, "--strategy", path, "--checkpoint", checkpoint)
    assert checkpoint.read_text().startswith("AOFCHK4\n")
    invoke(TRAIN, *args, "--iters", 100000, "--threads", 1, "--resume", checkpoint, "--strategy", resumed)
    invoke(TRAIN, *args, "--iters", 100000, "--threads", 4, "--strategy", full)
    assert resumed.read_bytes() == full.read_bytes()
    invoke(
        TRAIN,
        *args,
        "--iters",
        100000,
        "--rake-fixed",
        0.6,
        "--resume",
        checkpoint,
        "--strategy",
        path,
        success=False,
    )
    invoke(
        TRAIN,
        *SMALL,
        "--players",
        4,
        "--iters",
        100000,
        "--rake-percent",
        0,
        "--resume",
        checkpoint,
        "--strategy",
        path,
        success=False,
    )


@pytest.mark.parametrize("version", [2, 3])
def test_old_percentage_strategy_and_checkpoint_still_load(tmp_path, version):
    path, checkpoint, resumed, full = (
        tmp_path / n for n in ("old.bin", "old.checkpoint", "resume.bin", "full.bin")
    )
    args = [*SMALL, "--players", 4, "--rake-percent", 3]
    invoke(TRAIN, *args, "--strategy", path, "--checkpoint", checkpoint)
    before = load_strategy(path)
    payload = bytearray(path.read_bytes()[:-4])
    del payload[68:80]  # v4-only rake mode and fixed amount
    struct.pack_into("<I", payload, 8, version)
    if version == 3:
        payload += struct.pack("<I", zlib.crc32(payload))
    path.write_bytes(payload)
    assert load_strategy(path).params == before.params
    invoke(EVAL, "--strategy", path, "--samples", 1000, "--audit-samples", 1000)
    lines = checkpoint.read_text().splitlines()
    lines[0] = f"AOFCHK{version}"
    lines[1] = " ".join(lines[1].split()[:7])
    if version == 2:
        del lines[3]
    checkpoint.write_text("\n".join(lines) + "\n", encoding="ascii")
    invoke(TRAIN, *args, "--iters", 100000, "--resume", checkpoint, "--strategy", resumed)
    invoke(TRAIN, *args, "--iters", 100000, "--strategy", full)
    assert resumed.read_bytes() == full.read_bytes()


def test_legacy_sampled_engine_supports_zero_fixed_rake(tmp_path):
    path = tmp_path / "sampled.bin"
    invoke(TRAIN, *SMALL, "--engine", "sampled", "--rake-fixed", 0, "--strategy", path)
    doc = load_strategy(path)
    assert doc.params.rake_mode == "fixed" and doc.expected_rake == 0


@pytest.mark.parametrize("offset,fmt,value", [(68, "I", 9), (72, "d", -0.5), (72, "d", float("nan"))])
def test_invalid_fixed_metadata_rejected_even_with_valid_checksum(tmp_path, offset, fmt, value):
    path = tmp_path / "corrupt.bin"
    invoke(TRAIN, *SMALL, "--rake-fixed", 0.5, "--strategy", path)
    payload = bytearray(path.read_bytes())
    struct.pack_into("<" + fmt, payload, offset, value)
    struct.pack_into("<I", payload, len(payload) - 4, zlib.crc32(payload[:-4]))
    path.write_bytes(payload)
    with pytest.raises(ValueError):
        load_strategy(path)
    invoke(EVAL, "--strategy", path, "--samples", 1000, success=False)
