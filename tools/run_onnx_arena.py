from __future__ import annotations

import argparse
import hashlib
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
    parser.add_argument("--top-k", type=int, default=1)
    parser.add_argument("--policy-weight", type=float, default=1.0)
    parser.add_argument("--value-weight", type=float, default=0.0)
    args = parser.parse_args()
    if args.top_k <= 0 or args.policy_weight < 0.0 or args.value_weight < 0.0:
        raise ValueError("top-k and reranking weights must be non-negative")
    if args.policy_weight == 0.0 and args.value_weight == 0.0:
        raise ValueError("at least one reranking weight must be positive")

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
    opening_hashes: dict[int, str] = {}
    reranked_actions = 0
    candidate_positions = 0
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
        if fields[0] == "O":
            if len(fields) != 3:
                raise RuntimeError("invalid paired opening record")
            pair_id = int(fields[1])
            opening_hashes[pair_id] = hashlib.sha256(
                fields[2].encode("utf-8")
            ).hexdigest().upper()
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
            result["opening_sha256"] = opening_hashes.get(int(result["pair_id"]), "")
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

            # 将所有待走局面合并推理，CPU 线程留给并行 Alpha-Beta 搜索。
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
            if args.top_k == 1:
                for row, (game_id, ids) in enumerate(zip(game_ids, legal_ids, strict=True)):
                    action_id = int(ids[np.argmax(policy_logits[row, ids])])
                    process.stdin.write(f"A\t{game_id}\t{action_id}\n")
                process.stdin.flush()
                continue

            candidates_by_game: dict[int, np.ndarray] = {}
            policy_row_by_game: dict[int, int] = {}
            legal_by_game: dict[int, np.ndarray] = {}
            for row, (game_id, ids) in enumerate(zip(game_ids, legal_ids, strict=True)):
                candidate_count = min(args.top_k, len(ids))
                order = np.argsort(-policy_logits[row, ids], kind="stable")[:candidate_count]
                candidates = ids[order]
                candidates_by_game[game_id] = candidates
                policy_row_by_game[game_id] = row
                legal_by_game[game_id] = ids
                payload = ",".join(str(int(action_id)) for action_id in candidates)
                process.stdin.write(f"Q\t{game_id}\t{payload}\n")
            process.stdin.flush()

            value_header = process.stdout.readline().rstrip("\n").split("\t")
            if len(value_header) != 2 or value_header[0] != "V":
                raise RuntimeError("arena did not return candidate expansions")
            expanded_count = int(value_header[1])
            candidate_positions += expanded_count
            candidate_values: dict[tuple[int, int], float] = {}
            child_keys: list[tuple[int, int]] = []
            child_boards: list[np.ndarray] = []
            child_hands: list[np.ndarray] = []
            child_metas: list[np.ndarray] = []
            for _ in range(expanded_count):
                candidate_fields = process.stdout.readline().rstrip("\n").split("\t")
                if len(candidate_fields) != 5 or candidate_fields[0] != "C":
                    raise RuntimeError("invalid candidate expansion protocol")
                game_id = int(candidate_fields[1])
                action_id = int(candidate_fields[2])
                exact_value = int(candidate_fields[3])
                if action_id not in set(int(value) for value in candidates_by_game[game_id]):
                    raise RuntimeError("arena expanded an unrequested candidate")
                key = (game_id, action_id)
                if exact_value != 2:
                    candidate_values[key] = float(exact_value)
                else:
                    board, hand, meta = parse_state(candidate_fields[4])
                    child_keys.append(key)
                    child_boards.append(board)
                    child_hands.append(hand)
                    child_metas.append(meta)

            if child_keys:
                value_started = time.perf_counter()
                _, child_values = session.run(None, {
                    "board": np.stack(child_boards), "hand": np.stack(child_hands),
                    "meta": np.stack(child_metas),
                })
                inference_seconds += time.perf_counter() - value_started
                inference_positions += len(child_keys)
                inference_batches += 1
                batch_sizes.append(len(child_keys))
                if child_values.shape != (len(child_keys),) or not np.isfinite(child_values).all():
                    raise RuntimeError("ONNX returned invalid child values")
                for key, child_value in zip(child_keys, child_values, strict=True):
                    # 子局面轮到对手，取负号还原为当前走子方价值。
                    candidate_values[key] = -float(child_value)

            for game_id in game_ids:
                row = policy_row_by_game[game_id]
                candidates = candidates_by_game[game_id]
                best_policy = float(np.max(policy_logits[row, legal_by_game[game_id]]))
                scores = [
                    args.policy_weight * (float(policy_logits[row, action_id]) - best_policy)
                    + args.value_weight * candidate_values[(game_id, int(action_id))]
                    for action_id in candidates
                ]
                action_id = int(candidates[int(np.argmax(scores))])
                reranked_actions += int(action_id != int(candidates[0]))
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
    pair_scores: Counter[str] = Counter()
    for pair_id in range(args.games // 2):
        pair_games = [result for result in results if result["pair_id"] == pair_id]
        if len(pair_games) != 2 or any(result["truncated"] for result in pair_games):
            pair_scores["incomplete"] += 1
            continue
        points = sum(1.0 if result["neural_result"] == "win" else
                     0.5 if result["neural_result"] == "draw" else 0.0
                     for result in pair_games)
        pair_scores[f"{points:.1f}"] += 1
    summary = {
        "model": str(model_path), "model_sha256": manifest["onnx_sha256"],
        "rule_version": manifest["rule_version"], "provider": "CPUExecutionProvider",
        "games": args.games, "pairs": args.games // 2, "alpha_beta_depth": args.depth,
        "arena_commit": arena_commit,
        "opening_plies": args.opening_plies, "max_plies": args.max_plies,
        "threads": args.threads, "seed": args.seed,
        "selection": {"top_k": args.top_k, "policy_weight": args.policy_weight,
                      "value_weight": args.value_weight,
                      "reranked_actions": reranked_actions,
                      "candidate_positions": candidate_positions},
        "neural": {"wins": counts["win"], "draws": counts["draw"],
                   "losses": counts["loss"], "truncated": counts["truncated"],
                   "score_rate": ((counts["win"] + 0.5 * counts["draw"]) / completed
                                  if completed else 0.0),
                   "decisive_win_rate": counts["win"] / decisive if decisive else 0.0,
                   "decisive_win_rate_wilson95": wilson_interval(counts["win"], decisive)},
        "by_side": {side: dict(counter) for side, counter in side_counts.items()},
        "paired_score_points": dict(pair_scores),
        "unique_openings": len(set(opening_hashes.values())),
        "opening_pairs": len(opening_hashes),
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
