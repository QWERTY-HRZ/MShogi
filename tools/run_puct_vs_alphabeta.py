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


def main() -> int:
    parser = argparse.ArgumentParser(description="Evaluate PUCT ONNX against Alpha-Beta")
    parser.add_argument("--arena-exe", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--games", type=int, default=200)
    parser.add_argument("--depth", type=int, default=2)
    parser.add_argument("--threads", type=int, default=16)
    parser.add_argument("--simulations", type=int, required=True)
    parser.add_argument("--leaves-per-batch", type=int, default=4)
    parser.add_argument("--virtual-loss", type=float, default=1.0)
    parser.add_argument("--opening-plies", type=int, default=6)
    parser.add_argument("--max-plies", type=int, default=200)
    parser.add_argument("--seed", type=int, default=20260913)
    args = parser.parse_args()
    if (args.games <= 0 or args.games % 2 or args.depth <= 0 or
            args.simulations < 2 or args.leaves_per_batch <= 0 or args.threads <= 0):
        raise ValueError("invalid PUCT versus Alpha-Beta configuration")

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    model = args.model.resolve()
    if (manifest["rule_version"] != RULE_VERSION or
            int(manifest["action_count"]) != ACTION_COUNT or
            sha256_file(model) != manifest["onnx_sha256"]):
        raise ValueError("model and manifest are incompatible")
    command = [
        str(args.arena_exe.resolve()), "--runtime", str(args.runtime.resolve()),
        "--candidate", str(model), "--alpha-beta-depth", str(args.depth),
        "--threads", str(args.threads),
        "--games", str(args.games), "--simulations", str(args.simulations),
        "--leaves-per-batch", str(args.leaves_per_batch),
        "--virtual-loss", str(args.virtual_loss),
        "--opening-plies", str(args.opening_plies),
        "--max-plies", str(args.max_plies), "--seed", str(args.seed),
    ]
    started = time.perf_counter()
    process = subprocess.run(command, text=True, encoding="utf-8",
                             capture_output=True, check=False)
    if process.returncode != 0:
        raise RuntimeError(process.stderr.strip() or "PUCT versus Alpha-Beta failed")

    arena_commit = "unknown"
    openings: dict[int, str] = {}
    games: list[dict[str, object]] = []
    search_statistics: dict[str, int] = {}
    for line in process.stdout.splitlines():
        fields = line.split("\t")
        if fields[0] == "M":
            if (len(fields) != 7 or fields[2] != RULE_VERSION or
                    fields[6] != "alphabeta"):
                raise RuntimeError("PUCT arena metadata mismatch")
            arena_commit = fields[1]
        elif fields[0] == "O":
            openings[int(fields[1])] = hashlib.sha256(fields[2].encode()).hexdigest().upper()
        elif fields[0] == "R":
            side = fields[3]
            winner = int(fields[4])
            truncated = fields[7] == "1"
            result = (
                "truncated" if truncated else "draw" if winner == 0 else
                "win" if (winner == 1) == (side == "S") else "loss"
            )
            games.append({
                "game_id": int(fields[1]), "pair_id": int(fields[2]),
                "neural_side": side, "winner": winner, "end_reason": fields[5],
                "plies": int(fields[6]), "truncated": truncated,
                "neural_result": result,
                "opening_sha256": openings.get(int(fields[2]), ""),
            })
        elif fields[0] == "S" and fields[1] == "C":
            search_statistics = {
                "simulations": int(fields[2]), "inference_batches": int(fields[3]),
                "inference_positions": int(fields[4]),
                "tree_reuse_hits": int(fields[5]),
                "max_inference_batch": int(fields[6]),
            }
        elif fields[0] != "D":
            raise RuntimeError("unexpected PUCT arena protocol output")
    if len(games) != args.games or not search_statistics:
        raise RuntimeError("PUCT arena returned incomplete results")
    counts = Counter(str(game["neural_result"]) for game in games)
    completed = args.games - counts["truncated"]
    decisive = counts["win"] + counts["loss"]
    report = {
        "model": str(model), "model_sha256": manifest["onnx_sha256"],
        "rule_version": RULE_VERSION, "arena_commit": arena_commit,
        "games": args.games, "pairs": args.games // 2,
        "alpha_beta_depth": args.depth, "threads": args.threads,
        "simulations": args.simulations,
        "leaves_per_batch": args.leaves_per_batch,
        "opening_plies": args.opening_plies, "max_plies": args.max_plies,
        "seed": args.seed, "unique_openings": len(set(openings.values())),
        "results": {"wins": counts["win"], "draws": counts["draw"],
                    "losses": counts["loss"], "truncated": counts["truncated"],
                    "score_rate": (counts["win"] + 0.5 * counts["draw"]) /
                                  max(completed, 1),
                    "decisive_wilson95": wilson_interval(counts["win"], decisive)},
        "search_statistics": search_statistics,
        "elapsed_seconds": time.perf_counter() - started,
    }
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / "games.jsonl").write_text(
        "".join(json.dumps(game, ensure_ascii=False) + "\n" for game in games),
        encoding="utf-8",
    )
    (output / "summary.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
