"""Fixed-budget, multiple-seed optimizer comparison with fresh common audits."""

import argparse
import hashlib
import json
import platform
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "gui"))
from strategy_data import load_strategy  # noqa: E402


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--trainer", type=Path, default=ROOT / "build" / "aof2_train.exe")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--budget", type=int, default=100000000)
    parser.add_argument("--audit", type=int, default=20000000)
    parser.add_argument("--eval", type=int, default=2000000)
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--batch", type=int, default=4)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    report = {
        "schema": 1,
        "platform": platform.platform(),
        "trainer_sha256": hashlib.sha256(args.trainer.read_bytes()).hexdigest(),
        "runs": [],
    }
    for algorithm in ("none", "dcfr", "hs-dcfr"):
        for seed in (2026, 42, 7):
            output = args.output / f"strategy_{algorithm}_{seed}.bin"
            command = [
                str(args.trainer),
                "--players",
                "4",
                "--rake-percent",
                "3",
                "--discounting",
                algorithm,
                "--batch-samples",
                str(args.batch),
                "--iters",
                str(args.budget),
                "--audit-samples",
                str(args.audit),
                "--eval-samples",
                str(args.eval),
                "--threads",
                str(args.threads),
                "--seed",
                str(seed),
                "--log-every",
                "0",
                "--strategy",
                str(output),
            ]
            started = time.perf_counter()
            result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", check=True)
            elapsed = time.perf_counter() - started
            output.with_suffix(".log").write_text(result.stdout + result.stderr, encoding="utf-8")
            doc = load_strategy(output)
            row = {
                "algorithm": algorithm,
                "seed": seed,
                "seconds_including_audit": elapsed,
                "training_samples": doc.iterations,
                "gap_point": doc.metric,
                "gap_upper_99": doc.deviation_upper,
                "global_ev": doc.ev,
                "conditional_ev": doc.conditional_ev,
                "sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
                "command": command,
            }
            report["runs"].append(row)
            (args.output / "report.json").write_text(
                json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8"
            )
            print(json.dumps(row, ensure_ascii=False), flush=True)


if __name__ == "__main__":
    main()
