from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import time
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import onnxruntime as ort

from mshogi_ai.data import ACTION_COUNT, RULE_VERSION, parse_state, sha256_file
from run_onnx_arena import promotion_eligible, wilson_interval


@dataclass
class PendingPosition:
    game_id: int
    agent: str
    board: np.ndarray
    hand: np.ndarray
    meta: np.ndarray
    legal: np.ndarray
    logits: np.ndarray | None = None


def load_model(model_path: Path, manifest_path: Path) -> tuple[ort.InferenceSession, dict]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest["rule_version"] != RULE_VERSION or manifest["action_count"] != ACTION_COUNT:
        raise ValueError("model manifest is incompatible with the arena")
    if sha256_file(model_path) != manifest["onnx_sha256"]:
        raise ValueError("ONNX SHA-256 does not match its manifest")
    options = ort.SessionOptions()
    options.intra_op_num_threads = 1
    options.inter_op_num_threads = 1
    options.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
    return ort.InferenceSession(
        str(model_path.resolve()), sess_options=options,
        providers=["CPUExecutionProvider"],
    ), manifest


def select_reranked_action(logits: np.ndarray, legal: np.ndarray,
                           candidates: np.ndarray, values: dict[int, float],
                           policy_weight: float, value_weight: float) -> int:
    best_logit = float(np.max(logits[legal]))
    scores = [
        policy_weight * (float(logits[action]) - best_logit) +
        value_weight * values[int(action)]
        for action in candidates
    ]
    return int(candidates[int(np.argmax(scores))])


def main() -> int:
    parser = argparse.ArgumentParser(description="Paired candidate-versus-champion arena")
    parser.add_argument("--arena-exe", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--candidate-manifest", type=Path, required=True)
    parser.add_argument("--champion", type=Path, required=True)
    parser.add_argument("--champion-manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--games", type=int, default=1000)
    parser.add_argument("--opening-plies", type=int, default=6)
    parser.add_argument("--max-plies", type=int, default=200)
    parser.add_argument("--seed", type=int, default=20260912)
    parser.add_argument("--top-k", type=int, default=1)
    parser.add_argument("--policy-weight", type=float, default=1.0)
    parser.add_argument("--value-weight", type=float, default=0.5)
    parser.add_argument("--min-score-rate", type=float, default=0.55)
    parser.add_argument("--min-wilson-lower", type=float, default=0.50)
    args = parser.parse_args()
    if (args.games <= 0 or args.games % 2 or args.max_plies <= args.opening_plies or
            args.top_k <= 0 or args.policy_weight < 0.0 or args.value_weight < 0.0 or
            (args.policy_weight == 0.0 and args.value_weight == 0.0)):
        raise ValueError("games must be positive/even and max plies must exceed opening plies")

    sessions: dict[str, ort.InferenceSession] = {}
    sessions["C"], candidate_manifest = load_model(
        args.candidate.resolve(), args.candidate_manifest.resolve()
    )
    sessions["H"], champion_manifest = load_model(
        args.champion.resolve(), args.champion_manifest.resolve()
    )
    command = [
        str(args.arena_exe.resolve()), "--dual-neural", "--games", str(args.games),
        "--depth", "1", "--opening-plies", str(args.opening_plies),
        "--max-plies", str(args.max_plies), "--threads", "1",
        "--seed", str(args.seed),
    ]
    process = subprocess.Popen(
        command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, encoding="utf-8", bufsize=1,
    )
    if process.stdin is None or process.stdout is None or process.stderr is None:
        raise RuntimeError("failed to open arena protocol pipes")

    started = time.perf_counter()
    inference_seconds = 0.0
    inference_positions = 0
    inference_batches = 0
    openings: dict[int, str] = {}
    results: list[dict[str, object]] = []
    reranked_actions = 0
    candidate_positions = 0
    arena_commit = "unknown"
    while True:
        line = process.stdout.readline()
        if not line:
            raise RuntimeError(process.stderr.read().strip() or "arena ended unexpectedly")
        fields = line.rstrip("\n").split("\t")
        if fields[0] == "M":
            arena_commit = fields[1]
            if fields[2] != RULE_VERSION:
                raise RuntimeError("arena rule version mismatch")
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
        elif fields[0] == "B":
            count = int(fields[1])
            pending: list[PendingPosition] = []
            for _ in range(count):
                position = process.stdout.readline().rstrip("\n").split("\t")
                if len(position) != 6 or position[0] != "P" or position[2] not in sessions:
                    raise RuntimeError("invalid dual-neural position protocol")
                board, hand, meta = parse_state(position[4])
                legal = np.fromstring(position[5], dtype=np.int64, sep=",")
                if legal.size == 0 or np.any(legal < 0) or np.any(legal >= ACTION_COUNT):
                    raise RuntimeError("arena provided an invalid legal action list")
                pending.append(PendingPosition(
                    int(position[1]), position[2], board, hand, meta, legal
                ))

            for agent in ("C", "H"):
                group = [item for item in pending if item.agent == agent]
                if not group:
                    continue
                inference_started = time.perf_counter()
                logits, values = sessions[agent].run(None, {
                    "board": np.stack([item.board for item in group]),
                    "hand": np.stack([item.hand for item in group]),
                    "meta": np.stack([item.meta for item in group]),
                })
                inference_seconds += time.perf_counter() - inference_started
                inference_positions += len(group)
                inference_batches += 1
                if logits.shape != (len(group), ACTION_COUNT) or values.shape != (len(group),):
                    raise RuntimeError("model returned an unexpected output shape")
                if not np.isfinite(logits).all() or not np.isfinite(values).all():
                    raise RuntimeError("model returned NaN or infinity")
                for row, item in enumerate(group):
                    item.logits = logits[row]

            if args.top_k == 1:
                for item in pending:
                    assert item.logits is not None
                    selected = int(item.legal[np.argmax(item.logits[item.legal])])
                    process.stdin.write(f"A\t{item.game_id}\t{selected}\n")
                process.stdin.flush()
                continue

            candidates: dict[int, np.ndarray] = {}
            positions_by_game = {item.game_id: item for item in pending}
            for item in pending:
                assert item.logits is not None
                count_for_game = min(args.top_k, len(item.legal))
                order = np.argsort(-item.logits[item.legal], kind="stable")[:count_for_game]
                candidates[item.game_id] = item.legal[order]
                payload = ",".join(str(int(action)) for action in candidates[item.game_id])
                process.stdin.write(f"Q\t{item.game_id}\t{payload}\n")
            process.stdin.flush()

            value_header = process.stdout.readline().rstrip("\n").split("\t")
            if len(value_header) != 2 or value_header[0] != "V":
                raise RuntimeError("arena did not return candidate expansions")
            expanded_count = int(value_header[1])
            candidate_positions += expanded_count
            child_groups: dict[str, list[tuple[tuple[int, int], np.ndarray,
                                                    np.ndarray, np.ndarray]]] = {
                "C": [], "H": []
            }
            candidate_values: dict[tuple[int, int], float] = {}
            for _ in range(expanded_count):
                child = process.stdout.readline().rstrip("\n").split("\t")
                if len(child) != 5 or child[0] != "C":
                    raise RuntimeError("invalid candidate expansion protocol")
                game_id = int(child[1])
                action_id = int(child[2])
                exact_value = int(child[3])
                if game_id not in positions_by_game or action_id not in candidates[game_id]:
                    raise RuntimeError("arena returned an unrequested candidate")
                key = (game_id, action_id)
                if exact_value != 2:
                    candidate_values[key] = float(exact_value)
                else:
                    board, hand, meta = parse_state(child[4])
                    agent = positions_by_game[game_id].agent
                    child_groups[agent].append((key, board, hand, meta))

            for agent, group in child_groups.items():
                if not group:
                    continue
                inference_started = time.perf_counter()
                _, child_values = sessions[agent].run(None, {
                    "board": np.stack([item[1] for item in group]),
                    "hand": np.stack([item[2] for item in group]),
                    "meta": np.stack([item[3] for item in group]),
                })
                inference_seconds += time.perf_counter() - inference_started
                inference_positions += len(group)
                inference_batches += 1
                if child_values.shape != (len(group),) or not np.isfinite(child_values).all():
                    raise RuntimeError("model returned invalid child values")
                for item, value in zip(group, child_values, strict=True):
                    # 子局面轮到对手，取负号恢复为当前走子模型的视角。
                    candidate_values[item[0]] = -float(value)

            for item in pending:
                assert item.logits is not None
                game_candidates = candidates[item.game_id]
                values = {int(action): candidate_values[(item.game_id, int(action))]
                          for action in game_candidates}
                selected = select_reranked_action(
                    item.logits, item.legal, game_candidates, values,
                    args.policy_weight, args.value_weight,
                )
                reranked_actions += int(selected != int(game_candidates[0]))
                process.stdin.write(f"A\t{item.game_id}\t{selected}\n")
            process.stdin.flush()
        elif fields[0] == "D":
            break
        else:
            raise RuntimeError(f"unknown arena record: {fields[0]}")

    process.stdin.close()
    return_code = process.wait(timeout=30)
    if return_code != 0:
        raise RuntimeError(process.stderr.read().strip() or f"arena exited with {return_code}")
    if len(results) != args.games:
        raise RuntimeError("arena returned incomplete results")

    counts = Counter(str(item["candidate_result"]) for item in results)
    completed = args.games - counts["truncated"]
    decisive = counts["win"] + counts["loss"]
    score_rate = (counts["win"] + 0.5 * counts["draw"]) / max(completed, 1)
    interval = wilson_interval(counts["win"], decisive)
    eligible = promotion_eligible(
        args.games, counts["truncated"], score_rate, interval[0],
        args.min_score_rate, args.min_wilson_lower,
    )
    summary = {
        "arena_commit": arena_commit, "rule_version": RULE_VERSION,
        "games": args.games, "pairs": args.games // 2, "seed": args.seed,
        "opening_plies": args.opening_plies, "max_plies": args.max_plies,
        "unique_openings": len(set(openings.values())),
        "selection": {"top_k": args.top_k, "policy_weight": args.policy_weight,
                      "value_weight": args.value_weight,
                      "reranked_actions": reranked_actions,
                      "candidate_positions": candidate_positions},
        "candidate": {"model": str(args.candidate.resolve()),
                      "sha256": candidate_manifest["onnx_sha256"]},
        "champion": {"model": str(args.champion.resolve()),
                     "sha256": champion_manifest["onnx_sha256"]},
        "results": {"wins": counts["win"], "draws": counts["draw"],
                    "losses": counts["loss"], "truncated": counts["truncated"],
                    "score_rate": score_rate, "decisive_wilson95": interval},
        "promotion_gate": {
            "min_score_rate": args.min_score_rate,
            "min_decisive_wilson_lower": args.min_wilson_lower,
            "sample_size_met": args.games >= 1000,
            "decision": "promote_candidate" if eligible else "retain_champion",
        },
        "inference": {"seconds": inference_seconds, "positions": inference_positions,
                      "batches": inference_batches,
                      "positions_per_second": inference_positions / inference_seconds},
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
