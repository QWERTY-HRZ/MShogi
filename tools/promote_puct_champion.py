from __future__ import annotations

import argparse
import json
import os
import time
from collections import Counter
from pathlib import Path

from mshogi_ai.data import ACTION_COUNT, RULE_VERSION, sha256_file
from run_onnx_arena import wilson_interval


def validate_arena(summary_path: Path) -> tuple[dict[str, object], Path]:
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    gate = summary["promotion_gate"]
    if (gate.get("stage") != "promotion" or
            gate.get("decision") != "promote_candidate" or
            int(gate["required_games"]) < 1000 or
            float(gate["min_score_rate"]) < 0.55 or
            float(gate["min_decisive_wilson_lower"]) < 0.50 or
            int(summary["games"]) < int(gate["required_games"])):
        raise ValueError("arena report did not pass the production promotion gate")
    games_path = summary_path.parent / "games.jsonl"
    games = [json.loads(line) for line in games_path.read_text(encoding="utf-8").splitlines()]
    if len(games) != int(summary["games"]):
        raise ValueError("arena game record count mismatch")
    game_ids = [int(game["game_id"]) for game in games]
    if len(set(game_ids)) != len(games) or set(game_ids) != set(range(len(games))):
        # Arena 按终局先后输出，ID 无需有序，但必须无重复且完整覆盖。
        raise ValueError("arena game ids are duplicated or incomplete")
    counts = Counter(str(game["candidate_result"]) for game in games)
    for game in games:
        winner = int(game["winner"])
        expected_result = (
            "truncated" if game["truncated"] else "draw" if winner == 0 else
            "win" if (winner == 1) == (game["candidate_side"] == "S") else "loss"
        )
        if game["candidate_result"] != expected_result:
            raise ValueError("arena game has an inconsistent candidate result")
    expected = summary["results"]
    if ({"win": counts["win"], "draw": counts["draw"],
         "loss": counts["loss"], "truncated": counts["truncated"]} !=
            {"win": int(expected["wins"]), "draw": int(expected["draws"]),
             "loss": int(expected["losses"]),
             "truncated": int(expected["truncated"])}):
        raise ValueError("arena game results do not match the summary")
    if counts["truncated"]:
        raise ValueError("a champion cannot be promoted from truncated games")
    completed = len(games) - counts["truncated"]
    score_rate = (counts["win"] + 0.5 * counts["draw"]) / completed
    interval = wilson_interval(counts["win"], counts["win"] + counts["loss"])
    if (abs(score_rate - float(expected["score_rate"])) > 1.0e-12 or
            any(abs(value - recorded) > 1.0e-12 for value, recorded in
                zip(interval, expected["decisive_wilson95"], strict=True)) or
            score_rate < float(gate["min_score_rate"]) or
            interval[0] < float(gate["min_decisive_wilson_lower"])):
        raise ValueError("arena statistics do not satisfy the promotion gate")
    for pair_id in range(int(summary["pairs"])):
        pair = [game for game in games if int(game["pair_id"]) == pair_id]
        if (len(pair) != 2 or {game["candidate_side"] for game in pair} != {"S", "G"} or
                len({game["opening_sha256"] for game in pair}) != 1):
            raise ValueError(f"arena pair {pair_id} is not a valid side-swapped pair")
    return summary, games_path


def promote(summary_path: Path, candidate_manifest_path: Path,
            registry_path: Path, name: str) -> dict[str, object]:
    summary, games_path = validate_arena(summary_path)
    model_manifest = json.loads(candidate_manifest_path.read_text(encoding="utf-8"))
    model_path = Path(model_manifest["onnx"]).resolve()
    model_hash = sha256_file(model_path)
    if (model_manifest.get("rule_version") != RULE_VERSION or
            int(model_manifest.get("action_count", -1)) != ACTION_COUNT or
            model_hash != model_manifest.get("onnx_sha256") or
            model_hash != summary["candidate"]["sha256"] or
            model_path != Path(summary["candidate"]["model"]).resolve()):
        raise ValueError("candidate model, manifest and arena report do not match")

    if registry_path.exists():
        registry = json.loads(registry_path.read_text(encoding="utf-8"))
    else:
        registry = {"registry_version": 1, "rule_version": RULE_VERSION,
                    "action_count": ACTION_COUNT, "promotions": []}
    promotion = {
        "name": name, "promoted_at": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "model": str(model_path), "model_sha256": model_hash,
        "model_manifest": str(candidate_manifest_path.resolve()),
        "model_manifest_sha256": sha256_file(candidate_manifest_path),
        "checkpoint": model_manifest["checkpoint"],
        "checkpoint_sha256": model_manifest["checkpoint_sha256"],
        "training_commit": model_manifest.get("training_commit", "unknown"),
        "arena_summary": str(summary_path.resolve()),
        "arena_summary_sha256": sha256_file(summary_path),
        "arena_games": str(games_path.resolve()),
        "arena_games_sha256": sha256_file(games_path),
        "score_rate": summary["results"]["score_rate"],
        "decisive_wilson95": summary["results"]["decisive_wilson95"],
        "search": summary["search"],
    }
    existing = next((item for item in registry["promotions"]
                     if item["name"] == name), None)
    if existing:
        stable = lambda item: {key: value for key, value in item.items()
                               if key != "promoted_at"}
        if stable(existing) != stable(promotion):
            raise ValueError("promotion name already refers to different evidence")
        promotion = existing
    else:
        registry["promotions"].append(promotion)
    registry["active"] = promotion
    registry["updated_at"] = promotion["promoted_at"]
    registry_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = registry_path.with_suffix(".json.partial")
    temporary.write_text(json.dumps(registry, ensure_ascii=False, indent=2),
                         encoding="utf-8")
    # 证据、模型与逐局记录全部复核后，才原子切换训练冠军指针。
    os.replace(temporary, registry_path)
    return registry


def main() -> int:
    parser = argparse.ArgumentParser(description="Promote a verified PUCT training champion")
    parser.add_argument("--arena-summary", type=Path, required=True)
    parser.add_argument("--candidate-manifest", type=Path, required=True)
    parser.add_argument("--registry", type=Path, required=True)
    parser.add_argument("--name", required=True)
    args = parser.parse_args()
    registry = promote(args.arena_summary.resolve(), args.candidate_manifest.resolve(),
                       args.registry.resolve(), args.name)
    print(json.dumps(registry, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
