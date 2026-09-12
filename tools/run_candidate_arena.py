from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import time
from collections import Counter
from pathlib import Path

import numpy as np
import onnxruntime as ort

from mshogi_ai.data import ACTION_COUNT, RULE_VERSION, parse_state, sha256_file
from run_onnx_arena import wilson_interval


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
    parser.add_argument("--min-score-rate", type=float, default=0.55)
    parser.add_argument("--min-wilson-lower", type=float, default=0.50)
    args = parser.parse_args()
    if args.games <= 0 or args.games % 2 or args.max_plies <= args.opening_plies:
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
            pending: list[tuple[int, str, np.ndarray, np.ndarray, np.ndarray, np.ndarray]] = []
            for _ in range(count):
                position = process.stdout.readline().rstrip("\n").split("\t")
                if len(position) != 6 or position[0] != "P" or position[2] not in sessions:
                    raise RuntimeError("invalid dual-neural position protocol")
                board, hand, meta = parse_state(position[4])
                legal = np.fromstring(position[5], dtype=np.int64, sep=",")
                pending.append((int(position[1]), position[2], board, hand, meta, legal))

            selected: dict[int, int] = {}
            for agent in ("C", "H"):
                group = [item for item in pending if item[1] == agent]
                if not group:
                    continue
                inference_started = time.perf_counter()
                logits, values = sessions[agent].run(None, {
                    "board": np.stack([item[2] for item in group]),
                    "hand": np.stack([item[3] for item in group]),
                    "meta": np.stack([item[4] for item in group]),
                })
                inference_seconds += time.perf_counter() - inference_started
                inference_positions += len(group)
                inference_batches += 1
                if logits.shape != (len(group), ACTION_COUNT) or values.shape != (len(group),):
                    raise RuntimeError("model returned an unexpected output shape")
                if not np.isfinite(logits).all() or not np.isfinite(values).all():
                    raise RuntimeError("model returned NaN or infinity")
                for row, item in enumerate(group):
                    legal = item[5]
                    selected[item[0]] = int(legal[np.argmax(logits[row, legal])])
            for game_id, *_ in pending:
                process.stdin.write(f"A\t{game_id}\t{selected[game_id]}\n")
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
    eligible = (
        counts["truncated"] == 0 and score_rate >= args.min_score_rate and
        interval[0] >= args.min_wilson_lower
    )
    summary = {
        "arena_commit": arena_commit, "rule_version": RULE_VERSION,
        "games": args.games, "pairs": args.games // 2, "seed": args.seed,
        "opening_plies": args.opening_plies, "max_plies": args.max_plies,
        "unique_openings": len(set(openings.values())),
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
