from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import time
from collections import Counter
from pathlib import Path

from mshogi_ai.data import ACTION_COUNT, RULE_VERSION, sha256_file
from run_onnx_arena import wilson_interval


def verify_model(model: Path, manifest_path: Path) -> dict[str, object]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest["rule_version"] != RULE_VERSION or manifest["action_count"] != ACTION_COUNT:
        raise ValueError("model manifest is incompatible with the PUCT arena")
    if sha256_file(model) != manifest["onnx_sha256"]:
        raise ValueError("ONNX SHA-256 does not match its manifest")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description="Paired PUCT candidate-versus-champion arena")
    parser.add_argument("--arena-exe", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--candidate-manifest", type=Path, required=True)
    parser.add_argument("--champion", type=Path, required=True)
    parser.add_argument("--champion-manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--games", type=int, default=1000)
    parser.add_argument("--simulations", type=int, default=64)
    parser.add_argument("--c-puct", type=float, default=1.5)
    parser.add_argument("--opening-plies", type=int, default=6)
    parser.add_argument("--max-plies", type=int, default=200)
    parser.add_argument("--seed", type=int, default=20260912)
    parser.add_argument("--min-score-rate", type=float, default=0.55)
    parser.add_argument("--min-wilson-lower", type=float, default=0.50)
    args = parser.parse_args()
    if args.games <= 0 or args.games % 2 or args.simulations < 2:
        raise ValueError("games must be positive/even and simulations must be at least two")

    candidate_manifest = verify_model(args.candidate.resolve(),
                                      args.candidate_manifest.resolve())
    champion_manifest = verify_model(args.champion.resolve(),
                                     args.champion_manifest.resolve())
    command = [
        str(args.arena_exe.resolve()), "--runtime", str(args.runtime.resolve()),
        "--candidate", str(args.candidate.resolve()),
        "--champion", str(args.champion.resolve()), "--games", str(args.games),
        "--simulations", str(args.simulations), "--c-puct", str(args.c_puct),
        "--opening-plies", str(args.opening_plies),
        "--max-plies", str(args.max_plies), "--seed", str(args.seed),
    ]
    started = time.perf_counter()
    process = subprocess.run(command, text=True, encoding="utf-8",
                             capture_output=True, check=False)
    if process.returncode != 0:
        raise RuntimeError(process.stderr.strip() or "PUCT arena failed")

    arena_commit = "unknown"
    openings: dict[int, str] = {}
    results: list[dict[str, object]] = []
    search_stats: dict[str, dict[str, int]] = {}
    for line in process.stdout.splitlines():
        fields = line.split("\t")
        if fields[0] == "M":
            arena_commit = fields[1]
            if fields[2] != RULE_VERSION or int(fields[3]) != args.simulations:
                raise RuntimeError("PUCT arena metadata mismatch")
        elif fields[0] == "O":
            openings[int(fields[1])] = hashlib.sha256(fields[2].encode()).hexdigest().upper()
        elif fields[0] == "R":
            candidate_side = fields[3]
            winner = int(fields[4])
            truncated = fields[7] == "1"
            candidate_result = (
                "truncated" if truncated else "draw" if winner == 0 else
                "win" if (winner == 1) == (candidate_side == "S") else "loss"
            )
            results.append({
                "game_id": int(fields[1]), "pair_id": int(fields[2]),
                "candidate_side": candidate_side, "winner": winner,
                "end_reason": fields[5], "plies": int(fields[6]),
                "truncated": truncated, "candidate_result": candidate_result,
                "opening_sha256": openings.get(int(fields[2]), ""),
            })
        elif fields[0] == "S":
            search_stats[fields[1]] = {
                "simulations": int(fields[2]), "inference_batches": int(fields[3]),
                "inference_positions": int(fields[4]), "tree_reuse_hits": int(fields[5]),
            }
        elif fields[0] != "D":
            raise RuntimeError(f"unknown PUCT arena record: {fields[0]}")
    if len(results) != args.games or set(search_stats) != {"C", "H"}:
        raise RuntimeError("PUCT arena returned incomplete results")

    counts = Counter(str(item["candidate_result"]) for item in results)
    completed = args.games - counts["truncated"]
    decisive = counts["win"] + counts["loss"]
    score_rate = (counts["win"] + 0.5 * counts["draw"]) / max(completed, 1)
    interval = wilson_interval(counts["win"], decisive)
    eligible = (
        counts["truncated"] == 0 and score_rate >= args.min_score_rate and
        interval[0] >= args.min_wilson_lower
    )
    pair_points: Counter[str] = Counter()
    for pair_id in range(args.games // 2):
        pair = [item for item in results if item["pair_id"] == pair_id]
        if any(item["truncated"] for item in pair):
            pair_points["incomplete"] += 1
            continue
        points = sum(1.0 if item["candidate_result"] == "win" else
                     0.5 if item["candidate_result"] == "draw" else 0.0
                     for item in pair)
        pair_points[f"{points:.1f}"] += 1

    summary = {
        "arena_commit": arena_commit, "rule_version": RULE_VERSION,
        "games": args.games, "pairs": args.games // 2, "seed": args.seed,
        "search": {"simulations": args.simulations, "c_puct": args.c_puct,
                   "dirichlet_epsilon": 0.0, "temperature": 0.0},
        "opening_plies": args.opening_plies, "max_plies": args.max_plies,
        "unique_openings": len(set(openings.values())),
        "candidate": {"model": str(args.candidate.resolve()),
                      "sha256": candidate_manifest["onnx_sha256"],
                      "search_stats": search_stats["C"]},
        "champion": {"model": str(args.champion.resolve()),
                     "sha256": champion_manifest["onnx_sha256"],
                     "search_stats": search_stats["H"]},
        "results": {"wins": counts["win"], "draws": counts["draw"],
                    "losses": counts["loss"], "truncated": counts["truncated"],
                    "score_rate": score_rate, "decisive_wilson95": interval,
                    "paired_points": dict(pair_points)},
        "promotion_gate": {
            "production_games": 1000,
            "min_score_rate": args.min_score_rate,
            "min_decisive_wilson_lower": args.min_wilson_lower,
            "decision": "promote_candidate" if eligible else "retain_champion",
            "proof_only": args.games < 1000,
        },
        "elapsed_seconds": time.perf_counter() - started,
    }
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / "games.jsonl").write_text(
        "".join(json.dumps(item, ensure_ascii=False) + "\n" for item in results),
        encoding="utf-8",
    )
    (output / "summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
