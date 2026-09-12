from __future__ import annotations

import argparse
import json
import os
import time
from pathlib import Path

from mshogi_ai.data import ACTION_COUNT, RULE_VERSION, load_shard, sha256_file


def generation_record(name: str, weight: float, shard_paths: list[Path],
                      model_manifest_path: Path) -> dict[str, object]:
    if not name or weight <= 0.0 or not shard_paths:
        raise ValueError("generation name, positive weight and shards are required")
    model_manifest = json.loads(model_manifest_path.read_text(encoding="utf-8"))
    model_hash = str(model_manifest["onnx_sha256"])
    shards = [load_shard(path) for path in shard_paths]
    first = shards[0].metadata
    compatibility = {
        "format_version": first.get("format_version"),
        "rule_version": first.get("rule_version"),
        "action_count": first.get("action_count"),
        "policy_target": first.get("policy_target"),
        "replay_buffer_version": first.get("replay_buffer_version"),
    }
    expected = {
        "format_version": 3, "rule_version": RULE_VERSION,
        "action_count": ACTION_COUNT, "policy_target": "puct_visit_counts",
        "replay_buffer_version": 1,
    }
    if compatibility != expected:
        raise ValueError("generation contains an incompatible replay format")
    search_fields = (
        "simulations", "leaves_per_batch", "virtual_loss", "c_puct",
        "dirichlet_alpha", "dirichlet_epsilon", "temperature",
        "temperature_plies", "max_plies",
    )
    search = {field: first.get(field, 1 if field == "leaves_per_batch" else
                               0.0 if field == "virtual_loss" else None)
              for field in search_fields}
    for shard in shards:
        if str(shard.metadata.get("model_sha256")) != model_hash:
            raise ValueError("generation shard does not match the model manifest")
        candidate_search = {
            field: shard.metadata.get(field, 1 if field == "leaves_per_batch" else
                                      0.0 if field == "virtual_loss" else None)
            for field in search_fields
        }
        if candidate_search != search:
            raise ValueError("generation shards use different search settings")
    return {
        "name": name, "weight": weight, "added_at": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "model_sha256": model_hash, "model_manifest": str(model_manifest_path.resolve()),
        "search": search,
        "shards": [{
            "path": str(shard.path), "sha256": sha256_file(shard.path),
            "core_commit": shard.metadata["core_commit"], "seed": shard.metadata["seed"],
            "games": len(shard.games),
            "positions": sum(len(game.positions) for game in shard.games),
        } for shard in shards],
    }


def update_pool(pool_directory: Path, generation: dict[str, object],
                max_generations: int, max_positions: int) -> dict[str, object]:
    if max_generations <= 0 or max_positions <= 0:
        raise ValueError("pool limits must be positive")
    pool_directory.mkdir(parents=True, exist_ok=True)
    path = pool_directory / "pool_manifest.json"
    if path.exists():
        manifest = json.loads(path.read_text(encoding="utf-8"))
    else:
        manifest = {
            "manifest_version": 1,
            "compatibility": {
                "format_version": 3, "rule_version": RULE_VERSION,
                "action_count": ACTION_COUNT, "policy_target": "puct_visit_counts",
                "replay_buffer_version": 1,
            },
            "created_at": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
            "generations": [],
        }
    existing = next((item for item in manifest["generations"]
                     if item["name"] == generation["name"]), None)
    if existing:
        comparable = lambda item: {key: value for key, value in item.items()
                                   if key not in {"added_at", "normalized_weight"}}
        if comparable(existing) != comparable(generation):
            raise ValueError("generation name already exists with different content")
    else:
        known = {shard["sha256"] for item in manifest["generations"]
                 for shard in item["shards"]}
        if any(shard["sha256"] in known for shard in generation["shards"]):
            raise ValueError("a shard cannot belong to multiple replay generations")
        manifest["generations"].append(generation)

    evicted: list[str] = []
    def position_count() -> int:
        return sum(int(shard["positions"]) for item in manifest["generations"]
                   for shard in item["shards"])
    while (len(manifest["generations"]) > max_generations or
           (position_count() > max_positions and len(manifest["generations"]) > 1)):
        evicted.append(str(manifest["generations"].pop(0)["name"]))
    total_weight = sum(float(item["weight"]) for item in manifest["generations"])
    for item in manifest["generations"]:
        item["normalized_weight"] = float(item["weight"]) / total_weight
    manifest["limits"] = {
        "max_generations": max_generations, "max_positions": max_positions,
        "over_capacity_single_generation": position_count() > max_positions,
    }
    manifest["totals"] = {
        "generations": len(manifest["generations"]),
        "games": sum(int(shard["games"]) for item in manifest["generations"]
                     for shard in item["shards"]),
        "positions": position_count(), "evicted": evicted,
    }
    manifest["updated_at"] = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    temporary = path.with_suffix(".json.partial")
    temporary.write_text(json.dumps(manifest, ensure_ascii=False, indent=2),
                         encoding="utf-8")
    # 整代淘汰而不截断单局，保证训练/验证切分仍以完整对局为单位。
    os.replace(temporary, path)
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description="Maintain a weighted multi-generation replay pool")
    parser.add_argument("--pool", type=Path, required=True)
    parser.add_argument("--generation", required=True)
    parser.add_argument("--weight", type=float, default=1.0)
    parser.add_argument("--model-manifest", type=Path, required=True)
    parser.add_argument("--max-generations", type=int, default=3)
    parser.add_argument("--max-positions", type=int, default=500000)
    parser.add_argument("shards", nargs="+", type=Path)
    args = parser.parse_args()
    generation = generation_record(
        args.generation, args.weight, [path.resolve() for path in args.shards],
        args.model_manifest.resolve(),
    )
    manifest = update_pool(args.pool.resolve(), generation,
                           args.max_generations, args.max_positions)
    print(json.dumps(manifest, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
