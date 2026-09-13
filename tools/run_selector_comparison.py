from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

from mshogi_ai.data import sha256_file


def run_checked(command: list[str]) -> None:
    result = subprocess.run(command, text=True, encoding="utf-8",
                            capture_output=True, check=False)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip() or
                           "selector comparison child process failed")


def read_json(path: Path) -> dict[str, object]:
    return json.loads(path.read_text(encoding="utf-8"))


def opening_signature(directory: Path) -> list[tuple[int, str]]:
    games = [json.loads(line) for line in
             (directory / "games.jsonl").read_text(encoding="utf-8").splitlines()]
    pairs: dict[int, str] = {}
    for game in games:
        pair_id = int(game["pair_id"])
        digest = str(game["opening_sha256"])
        if pair_id in pairs and pairs[pair_id] != digest:
            raise ValueError("paired games do not share an opening")
        pairs[pair_id] = digest
    return sorted(pairs.items())


def result_summary(path: Path) -> dict[str, object]:
    report = read_json(path)
    return {
        "summary": str(path.resolve()), "summary_sha256": sha256_file(path),
        "wins": report["results"]["wins"], "draws": report["results"]["draws"],
        "losses": report["results"]["losses"],
        "truncated": report["results"]["truncated"],
        "score_rate": report["results"]["score_rate"],
        "decisive_wilson95": report["results"]["decisive_wilson95"],
        "elapsed_seconds": report["elapsed_seconds"],
    }


def common_result_summary(path: Path) -> dict[str, object]:
    report = read_json(path)
    results = report.get("results", report.get("neural"))
    wilson = (results["decisive_wilson95"] if "decisive_wilson95" in results
              else results["decisive_win_rate_wilson95"])
    return {
        "summary": str(path.resolve()), "summary_sha256": sha256_file(path),
        "wins": results["wins"], "draws": results["draws"],
        "losses": results["losses"], "truncated": results["truncated"],
        "score_rate": results["score_rate"],
        "decisive_wilson95": wilson,
        "elapsed_seconds": report["elapsed_seconds"],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare deployed and PUCT champions by selector")
    parser.add_argument("--arena-exe", type=Path, required=True)
    parser.add_argument("--puct-arena-exe", type=Path, required=True)
    parser.add_argument("--benchmark-exe", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--candidate-manifest", type=Path, required=True)
    parser.add_argument("--champion", type=Path, required=True)
    parser.add_argument("--champion-manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--games", type=int, default=200)
    parser.add_argument("--seed", type=int, default=20260913)
    parser.add_argument("--budgets", default="8,16,32,64")
    parser.add_argument("--latency-positions", type=int, default=100)
    args = parser.parse_args()
    budgets = [int(value) for value in args.budgets.split(",")]
    if args.games <= 0 or args.games % 2 or not budgets or min(budgets) < 2:
        raise ValueError("invalid selector comparison configuration")

    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    script_directory = Path(__file__).resolve().parent
    shared = [
        "--arena-exe", str(args.arena_exe.resolve()),
        "--candidate", str(args.candidate.resolve()),
        "--candidate-manifest", str(args.candidate_manifest.resolve()),
        "--champion", str(args.champion.resolve()),
        "--champion-manifest", str(args.champion_manifest.resolve()),
        "--games", str(args.games), "--opening-plies", "6", "--max-plies", "200",
        "--seed", str(args.seed),
    ]
    for name, top_k, value_weight in (("direct", 1, 0.0), ("top5", 5, 0.5)):
        directory = output / name
        run_checked([
            sys.executable, str(script_directory / "run_candidate_arena.py"),
            *shared, "--output", str(directory), "--top-k", str(top_k),
            "--policy-weight", "1.0", "--value-weight", str(value_weight),
        ])

    for budget in budgets:
        directory = output / f"puct_{budget}"
        run_checked([
            sys.executable, str(script_directory / "run_puct_candidate_arena.py"),
            "--arena-exe", str(args.puct_arena_exe.resolve()),
            "--runtime", str(args.runtime.resolve()),
            "--candidate", str(args.candidate.resolve()),
            "--candidate-manifest", str(args.candidate_manifest.resolve()),
            "--champion", str(args.champion.resolve()),
            "--champion-manifest", str(args.champion_manifest.resolve()),
            "--output", str(directory), "--games", str(args.games),
            "--simulations", str(budget), "--leaves-per-batch", "4",
            "--virtual-loss", "1", "--opening-plies", "6", "--max-plies", "200",
            "--seed", str(args.seed), "--stage", "proof",
        ])

    common_root = output / "common_alphabeta_d2"
    common_root.mkdir(exist_ok=True)
    for name, top_k, value_weight in (("direct", 1, 0.0), ("top5", 5, 0.5)):
        run_checked([
            sys.executable, str(script_directory / "run_onnx_arena.py"),
            "--arena-exe", str(args.arena_exe.resolve()),
            "--model", str(args.candidate.resolve()),
            "--manifest", str(args.candidate_manifest.resolve()),
            "--output", str(common_root / name), "--games", str(args.games),
            "--depth", "2", "--opening-plies", "6", "--max-plies", "200",
            "--threads", "16", "--seed", str(args.seed), "--top-k", str(top_k),
            "--policy-weight", "1.0", "--value-weight", str(value_weight),
        ])
    for budget in budgets:
        run_checked([
            sys.executable, str(script_directory / "run_puct_vs_alphabeta.py"),
            "--arena-exe", str(args.puct_arena_exe.resolve()),
            "--runtime", str(args.runtime.resolve()),
            "--model", str(args.candidate.resolve()),
            "--manifest", str(args.candidate_manifest.resolve()),
            "--output", str(common_root / f"puct_{budget}"),
            "--games", str(args.games), "--depth", "2",
            "--simulations", str(budget), "--leaves-per-batch", "4",
            "--virtual-loss", "1", "--opening-plies", "6", "--max-plies", "200",
            "--seed", str(args.seed),
        ])

    latency_path = output / "single_game_latency.json"
    run_checked([
        sys.executable, str(script_directory / "benchmark_puct_latency.py"),
        "--benchmark-exe", str(args.benchmark_exe.resolve()),
        "--runtime", str(args.runtime.resolve()),
        "--candidate", str(args.candidate.resolve()),
        "--candidate-manifest", str(args.candidate_manifest.resolve()),
        "--champion", str(args.champion.resolve()),
        "--champion-manifest", str(args.champion_manifest.resolve()),
        "--output", str(latency_path), "--budgets", args.budgets,
        "--positions", str(args.latency_positions), "--warmup", "8",
        "--leaves-per-batch", "4", "--virtual-loss", "1",
        "--seed", str(args.seed + 1),
    ])

    directories = [output / "direct", output / "top5"] + [
        output / f"puct_{budget}" for budget in budgets
    ]
    signatures = [opening_signature(directory) for directory in directories]
    if any(signature != signatures[0] for signature in signatures[1:]):
        raise ValueError("selector runs did not use identical paired openings")
    selectors = {
        directory.name: result_summary(directory / "summary.json")
        for directory in directories
    }
    common_directories = [common_root / "direct", common_root / "top5"] + [
        common_root / f"puct_{budget}" for budget in budgets
    ]
    common_signatures = [opening_signature(directory) for directory in common_directories]
    if any(signature != signatures[0] for signature in common_signatures):
        raise ValueError("common-opponent runs did not use the shared paired openings")
    common_results = {
        directory.name: common_result_summary(directory / "summary.json")
        for directory in common_directories
    }
    best_common_selector = max(
        common_results, key=lambda name: common_results[name]["score_rate"]
    )
    latency = read_json(latency_path)
    responsive_budgets = [
        budget for budget in budgets
        if latency["models"]["candidate"]["measurements"][str(budget)]["p95_ms"] <= 100.0
    ]
    report = {
        "report_version": 1, "games_per_selector": args.games,
        "pairs_per_selector": args.games // 2, "seed": args.seed,
        "unique_openings": len({digest for _, digest in signatures[0]}),
        "candidate": str(args.candidate.resolve()),
        "champion": str(args.champion.resolve()),
        "selectors": selectors,
        "common_opponent": {
            "name": "AlphaBeta-Baseline", "depth": 2,
            "results": common_results,
            "best_score_selector": best_common_selector,
        },
        "single_game_latency": str(latency_path),
        "single_game_latency_sha256": sha256_file(latency_path),
        "client_screen": {
            "p95_limit_ms": 100.0,
            "responsive_puct_budgets": responsive_budgets,
            "recommended_selector": best_common_selector,
        },
    }
    summary_path = output / "summary.json"
    summary_path.write_text(json.dumps(report, ensure_ascii=False, indent=2),
                            encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
