from __future__ import annotations

import argparse
import json
from pathlib import Path

from mshogi_ai.data import sha256_file


def read_json(path: Path) -> dict[str, object]:
    return json.loads(path.read_text(encoding="utf-8"))


def generation_summary(path: Path) -> dict[str, object]:
    manifest = read_json(path)
    totals = manifest["totals"]
    parameters = manifest["parameters"]
    positions = int(totals["positions"])
    simulations = int(parameters["simulations"])
    wall_seconds = float(manifest["wall_seconds"])
    search = [item["search_statistics"] for item in manifest["shards"]]
    return {
        "manifest": str(path.resolve()), "manifest_sha256": sha256_file(path),
        "games": int(totals["games"]), "positions": positions,
        "simulations_per_move": simulations,
        "leaves_per_batch": int(parameters["leaves_per_batch"]),
        "wall_seconds": wall_seconds,
        "games_per_second": int(totals["games"]) / wall_seconds,
        "simulations_per_second": positions * simulations / wall_seconds,
        "average_plies": positions / int(totals["games"]),
        "terminal_rate": 1.0 - int(totals["truncated"]) / int(totals["games"]),
        "sente_wins": int(totals["sente_wins"]),
        "gote_wins": int(totals["gote_wins"]),
        "draws": int(totals["draws"]), "truncated": int(totals["truncated"]),
        "tree_reuse_rate": sum(int(item["tree_reuse_hits"]) for item in search) /
                           positions,
        "inference_batches": sum(int(item["inference_batches"]) for item in search),
        "inference_positions": sum(int(item["inference_positions"]) for item in search),
        "max_inference_batch": max(int(item["max_inference_batch"]) for item in search),
        "policy": manifest["policy"], "model_sha256": manifest["model_sha256"],
        "core_commits": sorted({str(item["core_commit"]) for item in manifest["shards"]}),
    }


def screening_summary(name: str, path: Path) -> dict[str, object]:
    report = read_json(path)
    if (report["promotion_gate"]["stage"] != "screening" or
            report["promotion_gate"]["decision"] != "advance_to_promotion"):
        raise ValueError(f"screening candidate {name} did not pass")
    return {
        "name": name, "summary": str(path.resolve()),
        "summary_sha256": sha256_file(path),
        "model_sha256": report["candidate"]["sha256"],
        "games": report["games"], "seed": report["seed"],
        "wins": report["results"]["wins"], "draws": report["results"]["draws"],
        "losses": report["results"]["losses"],
        "truncated": report["results"]["truncated"],
        "score_rate": report["results"]["score_rate"],
        "decisive_wilson95": report["results"]["decisive_wilson95"],
        "champion_sha256": report["champion"]["sha256"],
        "search": report["search"],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare PUCT pilot generation and gates")
    parser.add_argument("--s64-manifest", type=Path, required=True)
    parser.add_argument("--s128-manifest", type=Path, required=True)
    parser.add_argument("--screen-s64", type=Path, required=True)
    parser.add_argument("--screen-s128", type=Path, required=True)
    parser.add_argument("--screen-mixed", type=Path, required=True)
    parser.add_argument("--promotion", type=Path, required=True)
    parser.add_argument("--architecture-control", type=Path, required=True)
    parser.add_argument("--architecture-expanded", type=Path, required=True)
    parser.add_argument("--architecture-control-training", type=Path, required=True)
    parser.add_argument("--architecture-expanded-training", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    generations = {
        "s64": generation_summary(args.s64_manifest.resolve()),
        "s128": generation_summary(args.s128_manifest.resolve()),
    }
    if generations["s64"]["model_sha256"] != generations["s128"]["model_sha256"]:
        raise ValueError("pilot generations did not use the same source champion")
    screenings = [
        screening_summary("candidate_s64", args.screen_s64.resolve()),
        screening_summary("candidate_s128", args.screen_s128.resolve()),
        screening_summary("candidate_mixed", args.screen_mixed.resolve()),
    ]
    if (len({item["seed"] for item in screenings}) != 1 or
            len({json.dumps(item["search"], sort_keys=True) for item in screenings}) != 1 or
            len({item["champion_sha256"] for item in screenings}) != 1):
        raise ValueError("screening candidates were not evaluated under identical conditions")
    # 先按得分率，再按 Wilson 下界确定唯一正式候选，规则在读取晋级赛前固定。
    selected = max(screenings,
                   key=lambda item: (item["score_rate"], item["decisive_wilson95"][0]))
    promotion = read_json(args.promotion.resolve())
    if (promotion["promotion_gate"]["decision"] != "promote_candidate" or
            promotion["candidate"]["sha256"] != selected["model_sha256"]):
        raise ValueError("formal promotion does not match the best screened candidate")
    control_arena = read_json(args.architecture_control.resolve())
    expanded_arena = read_json(args.architecture_expanded.resolve())
    control_training = read_json(args.architecture_control_training.resolve())
    expanded_training = read_json(args.architecture_expanded_training.resolve())
    if (control_arena["seed"] != expanded_arena["seed"] or
            control_arena["champion"]["sha256"] != expanded_arena["champion"]["sha256"] or
            control_arena["search"] != expanded_arena["search"]):
        raise ValueError("architecture candidates were not evaluated identically")
    report = {
        "report_version": 1,
        "generation": generations,
        "relative_efficiency": {
            "s128_over_s64_games_per_second":
                generations["s128"]["games_per_second"] /
                generations["s64"]["games_per_second"],
            "s128_over_s64_simulations_per_second":
                generations["s128"]["simulations_per_second"] /
                generations["s64"]["simulations_per_second"],
            "s128_entropy_delta": generations["s128"]["policy"]["mean_entropy"] -
                                  generations["s64"]["policy"]["mean_entropy"],
        },
        "screening": screenings,
        "selected_candidate": selected["name"],
        "promotion": {
            "summary": str(args.promotion.resolve()),
            "summary_sha256": sha256_file(args.promotion.resolve()),
            "games": promotion["games"], "wins": promotion["results"]["wins"],
            "draws": promotion["results"]["draws"],
            "losses": promotion["results"]["losses"],
            "truncated": promotion["results"]["truncated"],
            "score_rate": promotion["results"]["score_rate"],
            "decisive_wilson95": promotion["results"]["decisive_wilson95"],
            "decision": promotion["promotion_gate"]["decision"],
        },
        "architecture": {
            "control_64x6": {
                "parameters": 944975,
                "training_seconds": control_training["seconds"],
                "test_policy_top1": control_training["test"]["policy_top1"],
                "arena_score_rate": control_arena["results"]["score_rate"],
                "arena_wilson95": control_arena["results"]["decisive_wilson95"],
                "arena_seconds": control_arena["elapsed_seconds"],
                "decision": control_arena["promotion_gate"]["decision"],
            },
            "expanded_96x8": {
                "parameters": 1843663,
                "training_seconds": expanded_training["seconds"],
                "test_policy_top1": expanded_training["test"]["policy_top1"],
                "arena_score_rate": expanded_arena["results"]["score_rate"],
                "arena_wilson95": expanded_arena["results"]["decisive_wilson95"],
                "arena_seconds": expanded_arena["elapsed_seconds"],
                "decision": expanded_arena["promotion_gate"]["decision"],
            },
            "selected": "64x6",
            "reason": "96x8 failed the 200-game screen and was slower",
        },
    }
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
