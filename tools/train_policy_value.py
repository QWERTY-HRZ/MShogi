from __future__ import annotations

import argparse
import glob
import json
import random
import time
from pathlib import Path

import numpy as np
import torch
from torch.utils.data import DataLoader
from torch.utils.tensorboard import SummaryWriter

from mshogi_ai.data import MShogiDataset, RULE_VERSION, load_split_samples, sha256_file
from mshogi_ai.model import MShogiNet, ModelConfig, count_parameters, policy_value_loss


def resolve_paths(patterns: list[str]) -> list[Path]:
    paths: set[Path] = set()
    for pattern in patterns:
        matches = glob.glob(pattern)
        if matches:
            paths.update(Path(match).resolve() for match in matches)
        elif Path(pattern).exists():
            paths.add(Path(pattern).resolve())
    if not paths:
        raise FileNotFoundError("no self-play shards matched --data")
    return sorted(paths)


def set_reproducible_seed(seed: int) -> None:
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(seed)
    torch.use_deterministic_algorithms(True, warn_only=True)
    if hasattr(torch.backends, "cudnn"):
        torch.backends.cudnn.benchmark = False
        torch.backends.cudnn.deterministic = True


def run_loader(
    model: MShogiNet,
    loader: DataLoader,
    device: torch.device,
    optimizer: torch.optim.Optimizer | None,
    value_weight: float,
) -> dict[str, float]:
    training = optimizer is not None
    model.train(training)
    sample_count = 0
    policy_loss_sum = 0.0
    value_squared_sum = 0.0
    value_error_sum = 0.0
    value_count = 0.0
    correct = 0

    for batch in loader:
        batch = {name: tensor.to(device, non_blocking=True)
                 for name, tensor in batch.items()}
        if training:
            optimizer.zero_grad(set_to_none=True)
        with torch.set_grad_enabled(training):
            logits, value = model(batch["board"], batch["hand"], batch["meta"])
            loss, policy_loss, _ = policy_value_loss(
                logits, value, batch["legal_mask"], batch["policy_target"],
                batch["value_target"], batch["value_mask"], value_weight
            )
            if training:
                loss.backward()
                torch.nn.utils.clip_grad_norm_(model.parameters(), max_norm=5.0)
                optimizer.step()

        size = batch["board"].shape[0]
        sample_count += size
        policy_loss_sum += float(policy_loss.detach()) * size
        masked_logits = logits.detach().masked_fill(~batch["legal_mask"], -1.0e9)
        correct += int((masked_logits.argmax(1) == batch["policy_target"].argmax(1)).sum())
        value_errors = (value.detach() - batch["value_target"]).abs() * batch["value_mask"]
        value_squared_sum += float((value_errors.square()).sum())
        value_error_sum += float(value_errors.sum())
        value_count += float(batch["value_mask"].sum())

    if sample_count == 0:
        raise ValueError("data loader is empty")
    policy_average = policy_loss_sum / sample_count
    value_average = value_squared_sum / max(value_count, 1.0)
    return {
        "loss": policy_average + value_weight * value_average,
        "policy_loss": policy_average,
        "value_loss": value_average,
        "policy_top1": correct / sample_count,
        "value_mae": value_error_sum / max(value_count, 1.0),
        "samples": float(sample_count),
        "value_samples": value_count,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Train the Mixed-Shogi policy-value baseline")
    parser.add_argument("--data", nargs="+", required=True,
                        help="self-play shard paths or glob patterns")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--epochs", type=int, default=5)
    parser.add_argument("--batch-size", type=int, default=256)
    parser.add_argument("--learning-rate", type=float, default=1.0e-3)
    parser.add_argument("--weight-decay", type=float, default=1.0e-4)
    parser.add_argument("--value-weight", type=float, default=1.0)
    parser.add_argument("--teacher-temperature", type=float, default=1.0)
    parser.add_argument("--channels", type=int, default=64)
    parser.add_argument("--residual-blocks", type=int, default=6)
    parser.add_argument("--seed", type=int, default=20260911)
    parser.add_argument("--split-seed", type=int, default=20260911)
    parser.add_argument("--num-workers", type=int, default=0)
    parser.add_argument("--cpu-threads", type=int, default=0)
    parser.add_argument("--max-samples-per-split", type=int, default=0)
    parser.add_argument("--device", choices=("auto", "cpu", "cuda"), default="auto")
    args = parser.parse_args()
    if args.epochs <= 0 or args.batch_size <= 0:
        raise ValueError("epochs and batch size must be positive")

    set_reproducible_seed(args.seed)
    if args.cpu_threads > 0:
        torch.set_num_threads(args.cpu_threads)
    if args.device == "cuda" and not torch.cuda.is_available():
        raise RuntimeError("CUDA was requested but is unavailable")
    device = torch.device(
        "cuda" if args.device == "cuda" or
        (args.device == "auto" and torch.cuda.is_available()) else "cpu"
    )

    paths = resolve_paths(args.data)
    split_samples, shards, split_game_counts = load_split_samples(paths, args.split_seed)
    if args.max_samples_per_split > 0:
        split_samples = {
            name: samples[:args.max_samples_per_split]
            for name, samples in split_samples.items()
        }
    if any(not split_samples[name] for name in ("train", "validation", "test")):
        raise ValueError("train, validation, and test splits must all be non-empty")

    # 训练集追加确定性的旋转换手副本，验证与测试保持原始分布。
    datasets = {
        "train": MShogiDataset(split_samples["train"], args.teacher_temperature, True),
        "validation": MShogiDataset(
            split_samples["validation"], args.teacher_temperature, False
        ),
        "test": MShogiDataset(split_samples["test"], args.teacher_temperature, False),
    }
    generator = torch.Generator().manual_seed(args.seed)
    loaders = {
        name: DataLoader(
            dataset,
            batch_size=args.batch_size,
            shuffle=name == "train",
            num_workers=args.num_workers,
            pin_memory=device.type == "cuda",
            generator=generator if name == "train" else None,
        )
        for name, dataset in datasets.items()
    }

    model_config = ModelConfig(
        channels=args.channels, residual_blocks=args.residual_blocks
    )
    model = MShogiNet(model_config).to(device)
    optimizer = torch.optim.AdamW(
        model.parameters(), lr=args.learning_rate, weight_decay=args.weight_decay
    )
    scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=args.epochs)

    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    manifest = {
        "rule_version": RULE_VERSION,
        "seed": args.seed,
        "split_seed": args.split_seed,
        "device": str(device),
        "model": model_config.to_dict(),
        "parameters": count_parameters(model),
        "games": split_game_counts,
        "positions": {name: len(samples) for name, samples in split_samples.items()},
        "effective_train_samples": len(datasets["train"]),
        "shards": [
            {
                "path": str(shard.path),
                "sha256": sha256_file(shard.path),
                "games": len(shard.games),
                "core_commit": shard.metadata["core_commit"],
            }
            for shard in shards
        ],
        "arguments": vars(args) | {"output": str(output)},
    }
    (output / "run_config.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2, default=str), encoding="utf-8"
    )

    history: list[dict[str, object]] = []
    best_loss = float("inf")
    best_path = output / "best.pt"
    writer = SummaryWriter(log_dir=str(output / "tensorboard"))
    started = time.perf_counter()
    for epoch in range(1, args.epochs + 1):
        epoch_started = time.perf_counter()
        train_metrics = run_loader(model, loaders["train"], device, optimizer,
                                   args.value_weight)
        validation_metrics = run_loader(model, loaders["validation"], device, None,
                                        args.value_weight)
        scheduler.step()
        record = {
            "epoch": epoch,
            "seconds": time.perf_counter() - epoch_started,
            "learning_rate": optimizer.param_groups[0]["lr"],
            "train": train_metrics,
            "validation": validation_metrics,
        }
        history.append(record)
        for split_name, metrics in (("train", train_metrics),
                                    ("validation", validation_metrics)):
            for metric_name, value in metrics.items():
                writer.add_scalar(f"{split_name}/{metric_name}", value, epoch)
        print(json.dumps(record, ensure_ascii=False), flush=True)
        if validation_metrics["loss"] < best_loss:
            best_loss = validation_metrics["loss"]
            torch.save({
                "checkpoint_format": 1,
                "rule_version": RULE_VERSION,
                "model_config": model_config.to_dict(),
                "model_state": model.state_dict(),
                "epoch": epoch,
                "validation": validation_metrics,
            }, best_path)

    torch.save({
        "checkpoint_format": 1,
        "rule_version": RULE_VERSION,
        "model_config": model_config.to_dict(),
        "model_state": model.state_dict(),
        "optimizer_state": optimizer.state_dict(),
        "epoch": args.epochs,
    }, output / "last.pt")
    checkpoint = torch.load(best_path, map_location=device, weights_only=False)
    model.load_state_dict(checkpoint["model_state"])
    test_metrics = run_loader(model, loaders["test"], device, None, args.value_weight)
    summary = {
        "seconds": time.perf_counter() - started,
        "best_epoch": checkpoint["epoch"],
        "best_validation": checkpoint["validation"],
        "test": test_metrics,
        "best_checkpoint": str(best_path),
        "best_sha256": sha256_file(best_path),
    }
    writer.close()
    (output / "history.json").write_text(
        json.dumps(history, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    (output / "summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
