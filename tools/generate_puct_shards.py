from __future__ import annotations

import argparse
import json
import math
import os
import subprocess
import time
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from mshogi_ai.data import load_shard, sha256_file
from verify_selfplay import default_replay_executable, verify


@dataclass(frozen=True)
class GenerationConfig:
    executable: Path
    replay_executable: Path
    runtime: Path
    model: Path
    model_sha256: str
    output_directory: Path
    games_per_shard: int
    seed_base: int
    simulations: int
    leaves_per_batch: int
    virtual_loss: float
    c_puct: float
    dirichlet_alpha: float
    dirichlet_epsilon: float
    temperature: float
    temperature_plies: int
    max_plies: int


def parse_generator_output(output: str) -> dict[str, float]:
    values: dict[str, float] = {}
    for field in output.split():
        if "=" not in field:
            continue
        name, value = field.split("=", 1)
        try:
            values[name] = float(value)
        except ValueError:
            continue
    return values


def shard_path(config: GenerationConfig, index: int) -> Path:
    return config.output_directory / f"selfplay_{index:03d}.jsonl.gz"


def expected_metadata(config: GenerationConfig, index: int) -> dict[str, object]:
    return {
        "format_version": 3,
        "model_sha256": config.model_sha256,
        "seed": config.seed_base + index,
        "games": config.games_per_shard,
        "simulations": config.simulations,
        "leaves_per_batch": config.leaves_per_batch,
        "virtual_loss": config.virtual_loss,
        "c_puct": config.c_puct,
        "dirichlet_alpha": config.dirichlet_alpha,
        "dirichlet_epsilon": config.dirichlet_epsilon,
        "temperature": config.temperature,
        "temperature_plies": config.temperature_plies,
        "max_plies": config.max_plies,
    }


def reusable(path: Path, config: GenerationConfig, index: int) -> bool:
    if not path.exists():
        return False
    try:
        metadata = load_shard(path).metadata
    except (OSError, ValueError, KeyError, json.JSONDecodeError):
        return False
    return all(metadata.get(name) == value
               for name, value in expected_metadata(config, index).items())


def previous_statistics(config: GenerationConfig, index: int) -> tuple[float, dict]:
    manifest_path = config.output_directory / "manifest.json"
    if not manifest_path.exists():
        return 0.0, {}
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        item = next(shard for shard in manifest["shards"]
                    if int(shard["index"]) == index)
        return float(item["generation_seconds"]), dict(item["search_statistics"])
    except (OSError, ValueError, KeyError, StopIteration, json.JSONDecodeError):
        return 0.0, {}


def generate_one(config: GenerationConfig, index: int) -> dict[str, object]:
    output = shard_path(config, index)
    if reusable(output, config, index):
        seconds, statistics = previous_statistics(config, index)
        return {"index": index, "path": output, "generated": False,
                "seconds": seconds, "generator": statistics}
    partial = output.with_suffix(output.suffix + ".partial")
    if partial.exists():
        partial.unlink()
    command = [
        str(config.executable), "--runtime", str(config.runtime),
        "--model", str(config.model), "--model-sha256", config.model_sha256,
        "--games", str(config.games_per_shard), "--output", str(partial),
        "--seed", str(config.seed_base + index),
        "--simulations", str(config.simulations),
        "--leaves-per-batch", str(config.leaves_per_batch),
        "--virtual-loss", str(config.virtual_loss), "--c-puct", str(config.c_puct),
        "--dirichlet-alpha", str(config.dirichlet_alpha),
        "--dirichlet-epsilon", str(config.dirichlet_epsilon),
        "--temperature", str(config.temperature),
        "--temperature-plies", str(config.temperature_plies),
        "--max-plies", str(config.max_plies),
    ]
    started = time.perf_counter()
    result = subprocess.run(command, text=True, encoding="utf-8",
                            capture_output=True, check=False)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip() or
                           f"PUCT shard {index} failed")
    # 只在生成程序完整退出后替换正式分片，保留可恢复的中断语义。
    os.replace(partial, output)
    return {
        "index": index, "path": output, "generated": True,
        "seconds": time.perf_counter() - started,
        "generator": parse_generator_output(result.stdout),
    }


def summarize(config: GenerationConfig, result: dict[str, object]) -> dict[str, object]:
    path = Path(result["path"])
    replay = verify(path, config.replay_executable)
    shard = load_shard(path)
    winners: Counter[str] = Counter()
    reasons: Counter[str] = Counter()
    entropies: list[float] = []
    maxima: list[float] = []
    legal_counts: list[int] = []
    for game in shard.games:
        winners["truncated" if game.truncated else
                ("draw", "sente", "gote")[game.winner]] += 1
        reasons[game.end_reason] += 1
        for position in game.positions:
            visits = np.asarray([value for _, value in position.legal_actions],
                                dtype=np.float64)
            probabilities = visits[visits > 0] / visits.sum()
            entropies.append(float(-(probabilities * np.log(probabilities)).sum()))
            maxima.append(float(probabilities.max()))
            legal_counts.append(len(visits))
    generator = result["generator"]
    return {
        "index": result["index"], "path": str(path.resolve()),
        "sha256": replay["sha256"], "seed": shard.metadata["seed"],
        "core_commit": shard.metadata["core_commit"],
        "games": len(shard.games), "positions": replay["positions"],
        "winners": dict(winners), "end_reasons": dict(reasons),
        "truncated": winners["truncated"], "generated": result["generated"],
        "generation_seconds": result["seconds"], "core_replay": replay["core_result"],
        "search_statistics": generator,
        "policy": {
            "mean_entropy": float(np.mean(entropies)),
            "mean_effective_branches": float(np.mean(np.exp(entropies))),
            "mean_max_probability": float(np.mean(maxima)),
            "mean_legal_actions": float(np.mean(legal_counts)),
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate validated PUCT replay shards")
    parser.add_argument("--selfplay-exe", type=Path, required=True)
    parser.add_argument("--replay-exe", type=Path)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--model-manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--shards", type=int, default=4)
    parser.add_argument("--games-per-shard", type=int, default=500)
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--seed-base", type=int, required=True)
    parser.add_argument("--simulations", type=int, required=True)
    parser.add_argument("--leaves-per-batch", type=int, default=4)
    parser.add_argument("--virtual-loss", type=float, default=1.0)
    parser.add_argument("--c-puct", type=float, default=1.5)
    parser.add_argument("--dirichlet-alpha", type=float, default=0.3)
    parser.add_argument("--dirichlet-epsilon", type=float, default=0.25)
    parser.add_argument("--temperature", type=float, default=1.0)
    parser.add_argument("--temperature-plies", type=int, default=12)
    parser.add_argument("--max-plies", type=int, default=200)
    args = parser.parse_args()
    if min(args.shards, args.games_per_shard, args.workers, args.simulations,
           args.leaves_per_batch, args.max_plies) <= 0:
        raise ValueError("count arguments must be positive")

    model_manifest = json.loads(args.model_manifest.read_text(encoding="utf-8"))
    model_hash = str(model_manifest["onnx_sha256"])
    model = args.model.resolve()
    if sha256_file(model) != model_hash:
        raise ValueError("model does not match its manifest")
    config = GenerationConfig(
        args.selfplay_exe.resolve(),
        (args.replay_exe or default_replay_executable()).resolve(),
        args.runtime.resolve(), model, model_hash, args.output.resolve(),
        args.games_per_shard, args.seed_base, args.simulations,
        args.leaves_per_batch, args.virtual_loss, args.c_puct,
        args.dirichlet_alpha, args.dirichlet_epsilon, args.temperature,
        args.temperature_plies, args.max_plies,
    )
    config.output_directory.mkdir(parents=True, exist_ok=True)
    started = time.perf_counter()
    raw: list[dict[str, object]] = []
    with ThreadPoolExecutor(max_workers=min(args.workers, args.shards)) as executor:
        futures = {executor.submit(generate_one, config, index): index
                   for index in range(args.shards)}
        for future in as_completed(futures):
            item = future.result()
            raw.append(item)
            print(json.dumps({"completed": item["index"],
                              "seconds": item["seconds"]}), flush=True)
    with ThreadPoolExecutor(max_workers=min(args.workers, args.shards)) as executor:
        shards = list(executor.map(lambda item: summarize(config, item), raw))
    shards.sort(key=lambda item: int(item["index"]))
    total_positions = sum(int(item["positions"]) for item in shards)
    total_generation_seconds = sum(float(item["generation_seconds"]) for item in shards)
    totals = {
        "games": sum(int(item["games"]) for item in shards),
        "positions": total_positions,
        "sente_wins": sum(int(item["winners"].get("sente", 0)) for item in shards),
        "gote_wins": sum(int(item["winners"].get("gote", 0)) for item in shards),
        "draws": sum(int(item["winners"].get("draw", 0)) for item in shards),
        "truncated": sum(int(item["truncated"]) for item in shards),
    }
    weighted = lambda name: sum(
        float(item["policy"][name]) * int(item["positions"]) for item in shards
    ) / total_positions
    manifest = {
        "manifest_version": 1, "created_at": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "model": str(model), "model_sha256": model_hash,
        "generator": str(config.executable),
        "generator_sha256": sha256_file(config.executable),
        "parameters": vars(args) | {"output": str(config.output_directory)},
        "totals": totals,
        "policy": {name: weighted(name) for name in (
            "mean_entropy", "mean_effective_branches", "mean_max_probability",
            "mean_legal_actions")},
        "aggregate_generation_seconds": total_generation_seconds,
        "wall_seconds": time.perf_counter() - started,
        "shards": shards,
    }
    manifest_path = config.output_directory / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2,
                                        default=str), encoding="utf-8")
    print(json.dumps({"manifest": str(manifest_path), "totals": totals,
                      "policy": manifest["policy"],
                      "wall_seconds": manifest["wall_seconds"]},
                     ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
