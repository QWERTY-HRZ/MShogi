from __future__ import annotations

import argparse
import json
from pathlib import Path

import torch

from mshogi_ai.data import MShogiDataset, load_split_samples, sha256_file
from mshogi_ai.model import MShogiNet, ModelConfig
from train_policy_value import BatchLoader, resolve_paths, run_loader


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Evaluate a policy-value checkpoint on a fixed game split"
    )
    parser.add_argument("--checkpoint", type=Path, required=True)
    parser.add_argument("--data", nargs="+", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--split", choices=("train", "validation", "test"),
                        default="test")
    parser.add_argument("--split-seed", type=int, default=20260911)
    parser.add_argument("--batch-size", type=int, default=2048)
    parser.add_argument("--device", choices=("cpu", "cuda"), default="cuda")
    parser.add_argument("--amp-bf16", action="store_true")
    args = parser.parse_args()
    if args.batch_size <= 0:
        raise ValueError("batch size must be positive")
    if args.device == "cuda" and not torch.cuda.is_available():
        raise RuntimeError("CUDA was requested but is unavailable")

    paths = resolve_paths(args.data)
    samples, shards, game_counts = load_split_samples(paths, args.split_seed)
    dataset = MShogiDataset(samples[args.split])
    loader = BatchLoader(dataset, args.batch_size, False, args.split_seed)

    checkpoint_path = args.checkpoint.resolve()
    checkpoint = torch.load(checkpoint_path, map_location="cpu", weights_only=False)
    model = MShogiNet(ModelConfig(**checkpoint["model_config"]))
    model.load_state_dict(checkpoint["model_state"])
    device = torch.device(args.device)
    model.to(device)
    # 回归集统一按原始标签计分，训练时的终局加权不进入横向指标。
    metrics = run_loader(model, loader, device, None, 1.0, args.amp_bf16)
    report = {
        "rule_version": checkpoint["rule_version"],
        "checkpoint": str(checkpoint_path),
        "checkpoint_sha256": sha256_file(checkpoint_path),
        "training_commit": checkpoint.get("training_commit", "unknown"),
        "split": args.split,
        "split_seed": args.split_seed,
        "games": game_counts[args.split],
        "positions": len(dataset),
        "device": str(device),
        "amp_bf16": args.amp_bf16,
        "shards": [
            {"path": str(shard.path), "sha256": sha256_file(shard.path),
             "games": shard.games, "positions": shard.positions,
             "core_commit": shard.metadata["core_commit"]}
            for shard in shards
        ],
        "metrics": metrics,
    }
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
