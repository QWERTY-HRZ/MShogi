from __future__ import annotations

import argparse
import json
import math
import subprocess
import time
from collections import Counter
from pathlib import Path

import numpy as np
import onnxruntime as ort

from mshogi_ai.data import ACTION_COUNT, parse_state, sha256_file


def wilson_interval(successes: int, trials: int, z: float = 1.959963984540054) -> list[float]:
    if trials == 0:
        return [0.0, 0.0]
    proportion = successes / trials
    denominator = 1.0 + z * z / trials
    center = (proportion + z * z / (2.0 * trials)) / denominator
    margin = z * math.sqrt(
        proportion * (1.0 - proportion) / trials + z * z / (4.0 * trials * trials)
    ) / denominator
    lower = 0.0 if successes == 0 else max(0.0, center - margin)
    upper = 1.0 if successes == trials else min(1.0, center + margin)
    return [lower, upper]


def main() -> int:
    parser = argparse.ArgumentParser(description="Evaluate ONNX against Alpha-Beta")
    parser.add_argument("--arena-exe", type=Path, required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--games", type=int, default=1000)
    parser.add_argument("--depth", type=int, required=True)
    parser.add_argument("--opening-plies", type=int, default=6)
    parser.add_argument("--max-plies", type=int, default=200)
    parser.add_argument("--threads", type=int, default=16)
    parser.add_argument("--seed", type=int, default=20260912)
    args = parser.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    if manifest["rule_version"] != "v1.11.0" or manifest["action_count"] != ACTION_COUNT:
        raise ValueError("model manifest is incompatible with the current rules")
    model_path = args.model.resolve()
    if sha256_file(model_path) != manifest["onnx_sha256"]:
        raise ValueError("ONNX SHA-256 does not match the manifest")

    options = ort.SessionOptions()
    options.intra_op_num_threads = 1
    options.inter_op_num_threads = 1
    options.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
    session = ort.InferenceSession(
        str(model_path), sess_options=options, providers=["CPUExecutionProvider"]
    )
    command = [
        str(args.arena_exe.resolve()), "--games", str(args.games),
        "--depth", str(args.depth), "--opening-plies", str(args.opening_plies),
        "--max-plies", str(args.max_plies), "--threads", str(args.threads),
        "--seed", str(args.seed),
    ]
    process = subprocess.Popen(
        command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, encoding="utf-8", bufsize=1,
    )
    if process.stdin is None or process.stdout is None or process.stderr is None:
        raise RuntimeError("failed to open arena protocol pipes")

    results: list[dict[str, object]] = []
    inference_seconds = 0.0
    inference_positions = 0
    inference_batches = 0
    batch_sizes: list[int] = []
    arena_commit = "unknown"
    started = time.perf_counter()
    while True:
        line = process.stdout.readline()
        if not line:
            error = process.stderr.read().strip()
            raise RuntimeError(error or "arena process ended unexpectedly")
        fields = line.rstrip("\n").split("\t")
        if fields[0] == "M":
            if len(fields) != 3 or fields[2] != manifest["rule_version"]:
                raise RuntimeError("arena metadata is incompatible with the model")
            arena_commit = fields[1]
            continue
        if fields[0] == "R":
            result = {
                "game_id": int(fields[1]), "pair_id": int(fields[2]),
                "neural_side": fields[3], "winner": int(fields[4]),
                "end_reason": fields[5], "plies": int(fields[6]),
                "truncated": fields[7] == "1",
            }
            winner = result["winner"]
            result["neural_result"] = (
                "truncated" if result["truncated"] else
                "draw" if winner == 0 else
                "win" if (winner == 1) == (result["neural_side"] == "S") else "loss"
            )
            results.append(result)
            continue
        if fields[0] == "B":
            count = int(fields[1])
            game_ids: list[int] = []
            legal_ids: list[np.ndarray] = []
            boards: list[np.ndarray] = []
            hands: list[np.ndarray] = []
            metas: list[np.ndarray] = []
            for _ in range(count):
                position_fields = process.stdout.readline().rstrip("\n").split("\t")
                if len(position_fields) != 5 or position_fields[0] != "P":
                    raise RuntimeError("invalid arena position protocol")
                game_ids.append(int(position_fields[1]))
                board, hand, meta = parse_state(position_fields[3])
                boards.append(board)
                hands.append(hand)
                metas.append(meta)
                ids = np.fromstring(position_fields[4], dtype=np.int64, sep=",")
                if ids.size == 0 or np.any(ids < 0) or np.any(ids >= ACTION_COUNT):
                    raise RuntimeError("arena provided an invalid legal action list")
                legal_ids.append(ids)

            inference_started = time.perf_counter()
            policy_logits, values = session.run(None, {
                "board": np.stack(boards), "hand": np.stack(hands),
                "meta": np.stack(metas),
            })
            inference_seconds += time.perf_counter() - inference_started
            inference_positions += count
            inference_batches += 1
            batch_sizes.append(count)
            if policy_logits.shape != (count, ACTION_COUNT) or values.shape != (count,):
                raise RuntimeError("ONNX returned an unexpected output shape")
            if not np.isfinite(policy_logits).all() or not np.isfinite(values).all():
                raise RuntimeError("ONNX returned NaN or infinity")
            for row, (game_id, ids) in enumerate(zip(game_ids, legal_ids, strict=True)):
                action_id = int(ids[np.argmax(policy_logits[row, ids])])
                process.stdin.write(f"A\t{game_id}\t{action_id}\n")
            process.stdin.flush()
            continue
        if fields[0] == "D":
            if int(fields[1]) != args.games:
                raise RuntimeError("arena completion count mismatch")
            break
        raise RuntimeError(f"unknown arena protocol record: {fields[0]}")

    process.stdin.close()
    return_code = process.wait(timeout=30)
    stderr = process.stderr.read().strip()
    if return_code != 0:
        raise RuntimeError(stderr or f"arena exited with {return_code}")
    elapsed = time.perf_counter() - started
    if len(results) != args.games:
        raise RuntimeError("arena returned an incomplete result set")

    counts = Counter(str(result["neural_result"]) for result in results)
    reasons = Counter(str(result["end_reason"]) for result in results)
    side_counts = {
        side: Counter(str(result["neural_result"]) for result in results
                      if result["neural_side"] == side)
        for side in ("S", "G")
    }
    decisive = counts["win"] + counts["loss"]
    completed = args.games - counts["truncated"]
    summary = {
        "model": str(model_path), "model_sha256": manifest["onnx_sha256"],
        "rule_version": manifest["rule_version"], "provider": "CPUExecutionProvider",
        "games": args.games, "pairs": args.games // 2, "alpha_beta_depth": args.depth,
        "arena_commit": arena_commit,
        "opening_plies": args.opening_plies, "max_plies": args.max_plies,
        "threads": args.threads, "seed": args.seed,
        "neural": {"wins": counts["win"], "draws": counts["draw"],
                   "losses": counts["loss"], "truncated": counts["truncated"],
                   "score_rate": ((counts["win"] + 0.5 * counts["draw"]) / completed
                                  if completed else 0.0),
                   "decisive_win_rate": counts["win"] / decisive if decisive else 0.0,
                   "decisive_win_rate_wilson95": wilson_interval(counts["win"], decisive)},
        "by_side": {side: dict(counter) for side, counter in side_counts.items()},
        "end_reasons": dict(reasons),
        "average_plies": sum(int(result["plies"]) for result in results) / args.games,
        "truncated": sum(bool(result["truncated"]) for result in results),
        "illegal_actions": 0,
        "elapsed_seconds": elapsed,
        "games_per_second": args.games / elapsed,
        "onnx_inference": {
            "seconds": inference_seconds, "positions": inference_positions,
            "batches": inference_batches,
            "average_batch": sum(batch_sizes) / len(batch_sizes),
            "positions_per_second": inference_positions / inference_seconds,
        },
    }
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / "games.jsonl").write_text(
        "".join(json.dumps(result, ensure_ascii=False) + "\n" for result in results),
        encoding="utf-8",
    )
    (output / "summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
