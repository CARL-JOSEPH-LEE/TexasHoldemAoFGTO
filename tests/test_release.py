import json
import os
import struct
import subprocess
from pathlib import Path

import pytest
from strategy_data import load_strategy
from workspace import VERSION, Workspace

ROOT = Path(__file__).resolve().parents[1]
TRAIN = ROOT / "build" / ("aof2_train.exe" if os.name == "nt" else "aof2_train")
EVAL = TRAIN.with_name("aof2_eval.exe" if os.name == "nt" else "aof2_eval")


def invoke(exe, *args, success=True):
    result = subprocess.run([str(exe), *map(str, args)], capture_output=True, timeout=30)
    assert (result.returncode == 0) == success, result.stdout + result.stderr
    return result


@pytest.mark.parametrize("algorithm,method", [("dcfr", 3), ("hs-dcfr", 4)])
def test_experimental_checkpoint_keeps_algorithm_horizon_and_rng(tmp_path, algorithm, method):
    checkpoint = tmp_path / "state.checkpoint"
    partial, full, resumed = (tmp_path / name for name in ("partial.bin", "full.bin", "resumed.bin"))
    common = [
        "--players",
        4,
        "--rake-percent",
        3,
        "--discounting",
        algorithm,
        "--eval-samples",
        1000,
        "--audit-samples",
        1000,
        "--log-every",
        0,
    ]
    schedule = ["--schedule-sweeps", 6] if algorithm == "hs-dcfr" else []
    invoke(
        TRAIN,
        *common,
        *schedule,
        "--threads",
        1,
        "--iters",
        41912,
        "--strategy",
        partial,
        "--checkpoint",
        checkpoint,
    )
    # The resumed CLI omits the horizon; it must restore the original one.
    invoke(TRAIN, *common, "--threads", 3, "--iters", 125736, "--strategy", resumed, "--resume", checkpoint)
    invoke(TRAIN, *common, *schedule, "--threads", 2, "--iters", 125736, "--strategy", full)
    assert full.read_bytes() == resumed.read_bytes()
    assert load_strategy(full).training_method == method
    invoke(
        TRAIN,
        *common,
        "--discounting",
        "none",
        "--iters",
        200000,
        "--resume",
        checkpoint,
        "--strategy",
        partial,
        success=False,
    )


def test_strategy_checksum_catches_valid_looking_numeric_corruption(tmp_path):
    path = tmp_path / "strategy.bin"
    invoke(TRAIN, "--players", 4, "--iters", 1000, "--eval-samples", 1000, "--strategy", path)
    payload = bytearray(path.read_bytes())
    assert struct.unpack_from("<I", payload, 8)[0] == 4
    # Still a valid probability. Domain validation alone cannot detect this change.
    struct.pack_into("<d", payload, 292, 0.123456)
    path.write_bytes(payload)
    with pytest.raises(ValueError, match="校验"):
        load_strategy(path)
    assert b"checksum" in invoke(EVAL, "--strategy", path, "--samples", 1000, success=False).stderr


def test_focused_audit_reproducible_and_does_not_mutate_strategy(tmp_path):
    path = ROOT / "data" / "strategy_stratified_4p_10bb_rake3.bin"
    before = path.read_bytes()
    common = ["--strategy", path, "--node", 13, "--hand", "JTs", "--samples", 100000, "--seed", 1234]
    a = json.loads(invoke(EVAL, *common, "--threads", 1).stdout)
    b = json.loads(invoke(EVAL, *common, "--threads", 4).stdout)
    assert a == b
    assert a["samples"] >= 100000 and a["effective_samples"] > 1000
    assert a["lower"] <= a["action_ev"] - a["fold_ev"] <= a["upper"]
    assert a["std_error"] > 0 and a["confidence"] == 0.99
    assert path.read_bytes() == before
    invoke(EVAL, "--strategy", path, "--hand", "AA", success=False)
    invoke(EVAL, "--strategy", path, "--node", 13, "--hand", "AAo", success=False)


def test_workspace_durable_history_and_interruption_detection(tmp_path, monkeypatch):
    workspace = Workspace(tmp_path)
    output = tmp_path / "strategy.bin"
    job = workspace.start(output, [str(TRAIN), "--players", "4"], TRAIN)
    workspace.append_log(job, "训练日志\n")
    assert workspace.jobs()[0]["status"] == "running"
    assert "训练日志" in workspace.log(job)
    monkeypatch.setattr(Workspace, "_alive", staticmethod(lambda pid: False))
    restored = Workspace(tmp_path)
    assert restored.jobs()[0]["status"] == "interrupted"
    output.write_bytes(b"result")
    restored.finish(job, "succeeded", output=output)
    final = Workspace(tmp_path).jobs()[0]
    assert final["status"] == "succeeded" and len(final["result_hash"]) == 64
    assert final["engine_hash"] and final["command"] and final["checkpoint"]


def test_version_and_atomic_output_aliases(tmp_path):
    assert json.loads(invoke(TRAIN, "--version").stdout)["version"] == VERSION
    assert json.loads(invoke(EVAL, "--version").stdout)["strategy_schema"] == 4
    assert json.loads(invoke(EVAL, "--version").stdout)["fixed_rake"] is True
    output = tmp_path / "result.bin"
    invoke(TRAIN, "--strategy", output, "--checkpoint", str(output) + ".tmp", success=False)
    if os.name == "nt":
        invoke(TRAIN, "--strategy", output, "--csv", str(output).upper(), success=False)


def test_bundled_manifest_rejects_changed_old_strategy(tmp_path):
    import hashlib

    from workbench import load_bundle

    original = (ROOT / "data/strategy_stratified_4p_10bb_rake3.bin").read_bytes()
    path = tmp_path / "strategy_builtin.bin"
    path.write_bytes(original)
    (tmp_path / "bundle-manifest.json").write_text(
        json.dumps({path.name: hashlib.sha256(original).hexdigest()})
    )
    assert len(load_bundle(tmp_path).documents) == 1
    changed = bytearray(original)
    struct.pack_into("<d", changed, 280, 0.1)
    path.write_bytes(changed)
    bundle = load_bundle(tmp_path)
    assert not bundle.documents and "指纹" in bundle.errors[0]
