from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

import torch

from mshogi_ai.data import sha256_file
from mshogi_ai.model import MShogiNet, ModelConfig, count_parameters


def benchmark(
    model: MShogiNet,
    device: torch.device,
    batch_size: int,
    warmup: int,
    iterations: int,
) -> dict[str, float | int]:
    generator = torch.Generator(device=device).manual_seed(20260912)
    board = torch.randn(batch_size, 10, 6, 5, generator=generator, device=device)
    hand = torch.randn(batch_size, 2, 3, 4, generator=generator, device=device)
    meta = torch.randn(batch_size, 5, generator=generator, device=device)

    with torch.inference_mode():
        for _ in range(warmup):
            model(board, hand, meta)
        if device.type == "cuda":
            torch.cuda.synchronize(device)
            torch.cuda.reset_peak_memory_stats(device)
        started = time.perf_counter()
        for _ in range(iterations):
            model(board, hand, meta)
        if device.type == "cuda":
            torch.cuda.synchronize(device)
        elapsed = time.perf_counter() - started

    positions = batch_size * iterations
    result: dict[str, float | int] = {
        "batch_size": batch_size,
        "iterations": iterations,
        "seconds": elapsed,
        "batch_ms": elapsed * 1000.0 / iterations,
        "positions_per_second": positions / elapsed,
        "position_us": elapsed * 1_000_000.0 / positions,
    }
    if device.type == "cuda":
        result["peak_memory_mib"] = torch.cuda.max_memory_allocated(device) / 1048576.0
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="Benchmark Mixed-Shogi model inference")
    parser.add_argument("--checkpoint", type=Path, required=True)
    parser.add_argument("--devices", nargs="+", choices=("cpu", "cuda"), required=True)
    parser.add_argument("--batch-sizes", nargs="+", type=int,
                        default=(1, 64, 256, 1024))
    parser.add_argument("--warmup", type=int, default=10)
    parser.add_argument("--iterations", type=int, default=50)
    parser.add_argument("--cpu-threads", type=int, default=16)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if any(batch_size <= 0 for batch_size in args.batch_sizes):
        raise ValueError("batch sizes must be positive")
    if args.warmup < 0 or args.iterations <= 0 or args.cpu_threads <= 0:
        raise ValueError("invalid benchmark parameters")

    checkpoint_path = args.checkpoint.resolve()
    checkpoint = torch.load(checkpoint_path, map_location="cpu", weights_only=False)
    config = ModelConfig(**checkpoint["model_config"])
    torch.set_num_threads(args.cpu_threads)
    results: dict[str, object] = {
        "checkpoint": str(checkpoint_path),
        "checkpoint_sha256": sha256_file(checkpoint_path),
        "training_commit": checkpoint.get("training_commit", "unknown"),
        "torch_version": torch.__version__,
        "torch_cuda_runtime": torch.version.cuda,
        "cpu_threads": torch.get_num_threads(),
        "parameters": count_parameters(MShogiNet(config)),
        "devices": {},
    }

    for device_name in args.devices:
        if device_name == "cuda" and not torch.cuda.is_available():
            raise RuntimeError("CUDA benchmark requested but torch.cuda.is_available() is false")
        device = torch.device(device_name)
        model = MShogiNet(config).to(device)
        model.load_state_dict(checkpoint["model_state"])
        model.eval()
        # 每种设备都使用相同形状、预热次数和迭代次数，只测前向推理。
        device_results: dict[str, object] = {
            "name": torch.cuda.get_device_name(device) if device.type == "cuda" else "CPU",
            "batches": [
                benchmark(model, device, size, args.warmup, args.iterations)
                for size in args.batch_sizes
            ],
        }
        results["devices"][device_name] = device_results
        del model
        if device.type == "cuda":
            torch.cuda.empty_cache()

    rendered = json.dumps(results, ensure_ascii=False, indent=2)
    if args.output:
        output_path = args.output.resolve()
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(rendered, encoding="utf-8")
    print(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
