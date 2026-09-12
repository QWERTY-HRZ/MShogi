from __future__ import annotations

import argparse
import json
import os
import subprocess
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path

from mshogi_ai.data import load_shard, sha256_file
from verify_selfplay import default_replay_executable, verify


@dataclass(frozen=True)
class GenerationConfig:
    executable: Path
    replay_executable: Path
    output_directory: Path
    games_per_shard: int
    seed_base: int
    depth: int
    temperature: float
    temperature_plies: int
    max_plies: int


def default_selfplay_executable() -> Path:
    repository = Path(__file__).resolve().parents[1]
    build_root = repository.parent / "Build"
    candidates = [
        build_root / "MinGW_13_1_0-Release" / "src" / "mshogi_selfplay.exe",
        build_root / "Headless-Release" / "src" / "mshogi_selfplay.exe",
        build_root / "MinGW_13_1_0-Debug" / "src" / "mshogi_selfplay.exe",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    raise FileNotFoundError("mshogi_selfplay executable was not found")


def expected_seed(config: GenerationConfig, shard_index: int) -> int:
    return config.seed_base + shard_index


def shard_path(config: GenerationConfig, shard_index: int) -> Path:
    return config.output_directory / f"selfplay_{shard_index:03d}.jsonl.gz"


def is_reusable(path: Path, config: GenerationConfig, shard_index: int) -> bool:
    if not path.exists():
        return False
    try:
        shard = load_shard(path)
    except (OSError, ValueError, KeyError, json.JSONDecodeError):
        return False
    metadata = shard.metadata
    return (
        len(shard.games) == config.games_per_shard
        and int(metadata["seed"]) == expected_seed(config, shard_index)
        and int(metadata["search_depth"]) == config.depth
        and float(metadata["temperature"]) == config.temperature
        and int(metadata["temperature_plies"]) == config.temperature_plies
        and int(metadata["max_plies"]) == config.max_plies
    )


def generate_one(config: GenerationConfig, shard_index: int) -> dict[str, object]:
    output = shard_path(config, shard_index)
    if is_reusable(output, config, shard_index):
        return {"index": shard_index, "path": output, "generated": False,
                "seconds": 0.0}

    partial = output.with_suffix(output.suffix + ".partial")
    if partial.exists():
        partial.unlink()
    command = [
        str(config.executable), "--games", str(config.games_per_shard),
        "--output", str(partial), "--seed", str(expected_seed(config, shard_index)),
        "--depth", str(config.depth), "--temperature", str(config.temperature),
        "--temperature-plies", str(config.temperature_plies),
        "--max-plies", str(config.max_plies),
    ]
    started = time.perf_counter()
    result = subprocess.run(command, text=True, encoding="utf-8",
                            capture_output=True, check=False)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip() or
                           f"self-play shard {shard_index} failed")
    # 完整生成后再原子替换，进程中断不会留下被误用的正式分片。
    os.replace(partial, output)
    return {"index": shard_index, "path": output, "generated": True,
            "seconds": time.perf_counter() - started,
            "generator_output": result.stdout.strip()}


def summarize(config: GenerationConfig, result: dict[str, object]) -> dict[str, object]:
    path = Path(result["path"])
    replay = verify(path, config.replay_executable)
    shard = load_shard(path)
    winners = {"draw": 0, "sente": 0, "gote": 0}
    end_reasons: dict[str, int] = {}
    truncated = 0
    for game in shard.games:
        winners[("draw", "sente", "gote")[game.winner]] += 1
        end_reasons[game.end_reason] = end_reasons.get(game.end_reason, 0) + 1
        truncated += int(game.truncated)
    return {
        "index": result["index"],
        "path": str(path.resolve()),
        "sha256": replay["sha256"],
        "seed": int(shard.metadata["seed"]),
        "core_commit": shard.metadata["core_commit"],
        "games": len(shard.games),
        "positions": replay["positions"],
        "winners": winners,
        "end_reasons": end_reasons,
        "truncated": truncated,
        "generated": result["generated"],
        "generation_seconds": result["seconds"],
        "core_replay": replay["core_result"],
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate resumable, independently seeded self-play shards"
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--shards", type=int, default=20)
    parser.add_argument("--games-per-shard", type=int, default=1000)
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--seed-base", type=int, default=2026091500)
    parser.add_argument("--depth", type=int, default=2)
    parser.add_argument("--temperature", type=float, default=1.0)
    parser.add_argument("--temperature-plies", type=int, default=12)
    parser.add_argument("--max-plies", type=int, default=200)
    parser.add_argument("--selfplay-exe", type=Path)
    parser.add_argument("--replay-exe", type=Path)
    args = parser.parse_args()
    if min(args.shards, args.games_per_shard, args.workers, args.depth,
           args.max_plies) <= 0:
        raise ValueError("shard, game, worker, depth and ply counts must be positive")
    if args.temperature < 0.0 or args.temperature_plies < 0:
        raise ValueError("temperature arguments must be non-negative")

    config = GenerationConfig(
        (args.selfplay_exe or default_selfplay_executable()).resolve(),
        (args.replay_exe or default_replay_executable()).resolve(),
        args.output.resolve(), args.games_per_shard, args.seed_base, args.depth,
        args.temperature, args.temperature_plies, args.max_plies,
    )
    config.output_directory.mkdir(parents=True, exist_ok=True)
    started = time.perf_counter()
    raw_results: list[dict[str, object]] = []
    with ThreadPoolExecutor(max_workers=min(args.workers, args.shards)) as executor:
        futures = {
            executor.submit(generate_one, config, index): index
            for index in range(args.shards)
        }
        for future in as_completed(futures):
            result = future.result()
            raw_results.append(result)
            print(json.dumps({"completed": result["index"],
                              "generated": result["generated"]}), flush=True)

    # 重放校验同样并行，但只有全部通过后才写聚合清单。
    with ThreadPoolExecutor(max_workers=min(args.workers, args.shards)) as executor:
        shards = list(executor.map(lambda item: summarize(config, item), raw_results))
    shards.sort(key=lambda item: int(item["index"]))
    totals = {
        "games": sum(int(item["games"]) for item in shards),
        "positions": sum(int(item["positions"]) for item in shards),
        "truncated": sum(int(item["truncated"]) for item in shards),
        "sente_wins": sum(int(item["winners"]["sente"]) for item in shards),
        "gote_wins": sum(int(item["winners"]["gote"]) for item in shards),
        "draws": sum(int(item["winners"]["draw"]) for item in shards),
    }
    manifest = {
        "manifest_version": 1,
        "created_at": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "generator": str(config.executable),
        "generator_sha256": sha256_file(config.executable),
        "replay_executable": str(config.replay_executable),
        "replay_sha256": sha256_file(config.replay_executable),
        "parameters": {
            "shards": args.shards, "games_per_shard": args.games_per_shard,
            "workers": args.workers, "seed_base": args.seed_base,
            "depth": args.depth, "temperature": args.temperature,
            "temperature_plies": args.temperature_plies,
            "max_plies": args.max_plies,
        },
        "totals": totals,
        "wall_seconds": time.perf_counter() - started,
        "shards": shards,
    }
    manifest_path = config.output_directory / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2),
                             encoding="utf-8")
    print(json.dumps({"manifest": str(manifest_path), "totals": totals,
                      "wall_seconds": manifest["wall_seconds"]},
                     ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
