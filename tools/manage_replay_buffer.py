from __future__ import annotations

import argparse
import json
import os
import time
from pathlib import Path

from mshogi_ai.data import ACTION_COUNT, RULE_VERSION, load_shard, sha256_file


IDENTITY_FIELDS = (
    "format_version", "rule_version", "action_count", "policy_target",
    "replay_buffer_version", "model_sha256", "simulations", "c_puct",
    "dirichlet_alpha", "dirichlet_epsilon", "leaves_per_batch", "virtual_loss",
)


def replay_identity(metadata: dict[str, object]) -> dict[str, object]:
    normalized = metadata | {
        "leaves_per_batch": metadata.get("leaves_per_batch", 1),
        "virtual_loss": metadata.get("virtual_loss", 0.0),
    }
    identity = {name: normalized.get(name) for name in IDENTITY_FIELDS}
    if identity != identity | {
        "format_version": 3,
        "rule_version": RULE_VERSION,
        "action_count": ACTION_COUNT,
        "policy_target": "puct_visit_counts",
        "replay_buffer_version": 1,
    }:
        raise ValueError("shard is not a compatible PUCT v3 replay shard")
    if any(identity[name] is None for name in IDENTITY_FIELDS):
        raise ValueError("PUCT replay identity is incomplete")
    return identity


def add_shards(buffer_directory: Path, shard_paths: list[Path],
               model_manifest_path: Path) -> dict[str, object]:
    model_manifest = json.loads(model_manifest_path.read_text(encoding="utf-8"))
    expected_model_hash = str(model_manifest["onnx_sha256"]).upper()
    if len(expected_model_hash) != 64:
        raise ValueError("model manifest has an invalid ONNX SHA-256")

    loaded = [load_shard(path) for path in shard_paths]
    if not loaded:
        raise ValueError("at least one shard is required")
    identities = [replay_identity(shard.metadata) for shard in loaded]
    if any(identity != identities[0] for identity in identities[1:]):
        raise ValueError("replay shards use different model or search settings")
    if str(identities[0]["model_sha256"]).upper() != expected_model_hash:
        raise ValueError("replay shard model hash does not match model manifest")

    buffer_directory.mkdir(parents=True, exist_ok=True)
    manifest_path = buffer_directory / "buffer_manifest.json"
    if manifest_path.exists():
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        existing_identity = replay_identity(manifest["identity"])
        if existing_identity != identities[0]:
            raise ValueError("new shard is incompatible with this replay buffer version")
        manifest["identity"] = existing_identity
    else:
        manifest = {
            "manifest_version": 1,
            "created_at": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
            "identity": identities[0],
            "model_manifest": str(model_manifest_path.resolve()),
            "shards": [],
        }

    known_hashes = {str(item["sha256"]) for item in manifest["shards"]}
    for shard in loaded:
        digest = sha256_file(shard.path)
        if digest in known_hashes:
            continue
        manifest["shards"].append({
            "path": str(shard.path),
            "sha256": digest,
            "core_commit": shard.metadata["core_commit"],
            "seed": shard.metadata["seed"],
            "games": len(shard.games),
            "positions": sum(len(game.positions) for game in shard.games),
        })
        known_hashes.add(digest)
    manifest["totals"] = {
        "shards": len(manifest["shards"]),
        "games": sum(int(item["games"]) for item in manifest["shards"]),
        "positions": sum(int(item["positions"]) for item in manifest["shards"]),
    }
    manifest["updated_at"] = time.strftime("%Y-%m-%dT%H:%M:%S%z")

    temporary_path = manifest_path.with_suffix(".json.partial")
    temporary_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    # 清单完整写入后再替换，异常中断不会破坏已有 replay buffer。
    os.replace(temporary_path, manifest_path)
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description="Create or extend a PUCT replay buffer")
    parser.add_argument("--buffer", type=Path, required=True)
    parser.add_argument("--model-manifest", type=Path, required=True)
    parser.add_argument("shards", nargs="+", type=Path)
    args = parser.parse_args()
    manifest = add_shards(
        args.buffer.resolve(), [path.resolve() for path in args.shards],
        args.model_manifest.resolve(),
    )
    print(json.dumps(manifest, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
