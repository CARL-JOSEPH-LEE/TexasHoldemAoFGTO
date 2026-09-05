"""Reproducible sequential baseline comparison; never compares training-sample EVs."""

import argparse
import itertools
import json
import os
import re
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "gui"))
from strategy_data import HandClass, load_strategy  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--players", type=int, choices=(2, 3, 4), default=4)
    parser.add_argument("--rake-percent", type=float, default=3)
    parser.add_argument("--samples", type=int, default=100000000)
    parser.add_argument("--evaluation-samples", type=int, default=20000000)
    parser.add_argument("--audit-samples", type=int, default=200000000)
    parser.add_argument("--seeds", type=int, nargs="+", default=[2026, 42, 7])
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument(
        "--engines", choices=("sampled", "stratified"), nargs="+", default=["sampled", "stratified"]
    )
    parser.add_argument(
        "--output", type=Path, default=ROOT / "benchmarks" / datetime.now().strftime("%Y%m%d_%H%M%S")
    )
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    extension = ".exe" if os.name == "nt" else ""
    results, policies = [], {}
    for engine in args.engines:
        policies[engine] = []
        for seed in args.seeds:
            name = f"{engine}_seed{seed}"
            path = (args.output / (name + ".bin")).resolve()
            command = [
                str(ROOT / "build" / ("aof2_train" + extension)),
                "--engine",
                engine,
                "--players",
                str(args.players),
                "--rake-percent",
                str(args.rake_percent),
                "--iters",
                str(args.samples),
                "--eval-samples",
                "10000",
                "--audit-samples",
                "10000",
                "--seed",
                str(seed),
                "--threads",
                str(args.threads),
                "--log-every",
                "0",
                "--strategy",
                str(path),
            ]
            print(f"Training {name}", flush=True)
            start = time.monotonic()
            trained = subprocess.run(command, check=True, capture_output=True, text=True, encoding="utf-8")
            elapsed = time.monotonic() - start
            log = trained.stdout + trained.stderr
            (args.output / (name + ".train.log")).write_text(log, encoding="utf-8")
            document = load_strategy(path)
            policies[engine].append(document)
            # One common, held-out evaluator for BOTH algorithms. Evaluating on
            # the training sample distribution would conceal overfitting/noise.
            evaluated = subprocess.run(
                [
                    str(ROOT / "build" / ("aof2_eval" + extension)),
                    "--strategy",
                    str(path),
                    "--samples",
                    str(args.evaluation_samples),
                    "--audit-samples",
                    str(args.audit_samples),
                    "--seed",
                    "98765",
                    "--threads",
                    str(args.threads),
                    "--csv",
                    str(path.with_suffix(".csv")),
                ],
                check=True,
                capture_output=True,
                text=True,
                encoding="utf-8",
            )
            (args.output / (name + ".eval.log")).write_text(evaluated.stdout, encoding="utf-8")
            gap = float(re.search(r"Sampled deviation sum=([\deE.+-]+)", evaluated.stdout).group(1))
            upper = float(re.search(r"deviation upper=([\deE.+-]+)", evaluated.stdout).group(1))
            training_seconds = float(
                re.search(r"\[(?:sampled|stratified)-cfr\].*seconds=([\deE.+-]+)", log).group(1)
            )
            results.append(
                {
                    "engine": engine,
                    "seed": seed,
                    "actual_samples": document.iterations,
                    "train_seconds": training_seconds,
                    "train_process_seconds": elapsed,
                    "heldout_gap": gap,
                    "heldout_99pct_upper": upper,
                    "command": command,
                }
            )
    stability = {}
    for engine, docs in policies.items():
        comparisons = []
        for a, b in itertools.combinations(docs, 2):
            differences = [
                (abs(x - y), h)
                for na, nb in zip(a.nodes, b.nodes)
                for h, (x, y) in enumerate(zip(na.frequency, nb.frequency))
            ]
            comparisons.append(
                {
                    "seeds": [a.seed, b.seed],
                    "cells_over_20_percentage_points": sum(d > 0.2 for d, h in differences),
                    "equal_node_combo_weighted_mean_difference_pp": 100
                    * sum(d * HandClass.from_index(h).num_combos() for d, h in differences)
                    / (1326 * len(a.nodes)),
                    "maximum_difference_pp": 100 * max(d for d, h in differences),
                }
            )
        stability[engine] = comparisons
    report = {
        "settings": {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()},
        "note": "Sample units differ: baseline=full chance deal; stratified=conditional terminal sample. Compare time and held-out error, not counts alone. Frequency agreement is not a Nash proof.",
        "runs": results,
        "seed_stability": stability,
    }
    (args.output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"Saved {args.output / 'report.json'}", flush=True)


if __name__ == "__main__":
    main()
