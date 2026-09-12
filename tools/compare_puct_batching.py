from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

import numpy as np

from mshogi_ai.data import ACTION_COUNT, load_shard, sha256_file


def root_policies(path: Path) -> tuple[dict[str, object], list[np.ndarray]]:
    shard = load_shard(path)
    policies: list[np.ndarray] = []
    for game in shard.games:
        position = game.positions[0]
        visits = np.zeros(ACTION_COUNT, dtype=np.float64)
        for action, count in position.legal_actions:
            visits[action] = count
        visits /= visits.sum()
        policies.append(visits)
    return shard.metadata, policies


def divergence(reference: np.ndarray, candidate: np.ndarray) -> tuple[float, float]:
    midpoint = 0.5 * (reference + candidate)
    def kl(left: np.ndarray, right: np.ndarray) -> float:
        mask = left > 0.0
        return float(np.sum(left[mask] * np.log(left[mask] / right[mask])))
    return 0.5 * (kl(reference, midpoint) + kl(candidate, midpoint)), \
           0.5 * float(np.abs(reference - candidate).sum())


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare single- and multi-leaf PUCT batching")
    parser.add_argument("--run", action="append", nargs=3,
                        metavar=("NAME", "SHARD", "SECONDS"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if len(args.run) < 2:
        raise ValueError("at least two batching runs are required")

    runs: dict[str, dict[str, object]] = {}
    policies: dict[str, list[np.ndarray]] = {}
    for name, path_text, seconds_text in args.run:
        path = Path(path_text).resolve()
        metadata, policy = root_policies(path)
        seconds = float(seconds_text)
        runs[name] = {
            "path": str(path), "sha256": sha256_file(path),
            "games": len(policy), "seconds": seconds,
            "games_per_second": len(policy) / seconds,
            "simulations": metadata["simulations"],
            "leaves_per_batch": metadata.get("leaves_per_batch", 1),
            "virtual_loss": metadata.get("virtual_loss", 0.0),
            "model_sha256": metadata["model_sha256"], "seed": metadata["seed"],
        }
        policies[name] = policy
    reference_name = args.run[0][0]
    reference_run = runs[reference_name]
    comparisons = {}
    for name, run in runs.items():
        if (run["games"] != reference_run["games"] or
                run["simulations"] != reference_run["simulations"] or
                run["model_sha256"] != reference_run["model_sha256"] or
                run["seed"] != reference_run["seed"]):
            raise ValueError("batching runs do not share model, seed, games and simulations")
        if name == reference_name:
            continue
        distances = [divergence(left, right) for left, right in
                     zip(policies[reference_name], policies[name], strict=True)]
        argmax_matches = sum(int(np.argmax(left) == np.argmax(right)) for left, right in
                             zip(policies[reference_name], policies[name], strict=True))
        # 只比较相同初始局面的根访问分布，避免后续不同走法造成状态错配。
        comparisons[name] = {
            "mean_jensen_shannon": float(np.mean([item[0] for item in distances])),
            "mean_total_variation": float(np.mean([item[1] for item in distances])),
            "root_argmax_match_rate": argmax_matches / len(distances),
            "speedup_over_reference": reference_run["seconds"] / run["seconds"],
        }
    report = {"report_version": 1, "reference": reference_name,
              "runs": runs, "root_policy_comparisons": comparisons}
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
