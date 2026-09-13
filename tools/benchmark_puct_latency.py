from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path

from mshogi_ai.data import ACTION_COUNT, RULE_VERSION, sha256_file


def verify_model(model: Path, manifest_path: Path) -> dict[str, object]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if (manifest["rule_version"] != RULE_VERSION or
            int(manifest["action_count"]) != ACTION_COUNT or
            sha256_file(model) != manifest["onnx_sha256"]):
        raise ValueError("model and manifest are incompatible")
    return manifest


def run_model(executable: Path, runtime: Path, model: Path, budgets: list[int],
              positions: int, warmup: int, leaves: int, virtual_loss: float,
              seed: int) -> dict[str, object]:
    command = [
        str(executable), "--runtime", str(runtime), "--model", str(model),
        "--budgets", ",".join(str(value) for value in budgets),
        "--positions", str(positions), "--warmup", str(warmup),
        "--leaves-per-batch", str(leaves), "--virtual-loss", str(virtual_loss),
        "--seed", str(seed),
    ]
    result = subprocess.run(command, text=True, encoding="utf-8",
                            capture_output=True, check=False)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "PUCT latency benchmark failed")
    core_commit = "unknown"
    model_load_ms = 0.0
    measurements: dict[str, object] = {}
    for line in result.stdout.splitlines():
        fields = line.split("\t")
        if fields[0] == "M" and len(fields) == 4:
            core_commit = fields[1]
            model_load_ms = float(fields[3])
            if fields[2] != RULE_VERSION:
                raise RuntimeError("benchmark rule version mismatch")
        elif fields[0] == "B" and len(fields) == 13:
            budget = int(fields[1])
            measurements[str(budget)] = {
                "positions": int(fields[2]), "games_started": int(fields[3]),
                "cold_tree_ms": float(fields[4]),
                "mean_ms": float(fields[5]), "p50_ms": float(fields[6]),
                "p95_ms": float(fields[7]), "max_ms": float(fields[8]),
                "inference_batches": int(fields[9]),
                "inference_positions": int(fields[10]),
                "tree_reuse_hits": int(fields[11]),
                "max_inference_batch": int(fields[12]),
            }
        else:
            raise RuntimeError("unexpected PUCT latency protocol output")
    if set(measurements) != {str(value) for value in budgets}:
        raise RuntimeError("PUCT latency benchmark returned incomplete budgets")
    return {"core_commit": core_commit, "model_load_ms": model_load_ms,
            "measurements": measurements}


def main() -> int:
    parser = argparse.ArgumentParser(description="Benchmark single-game PUCT CPU latency")
    parser.add_argument("--benchmark-exe", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--candidate-manifest", type=Path, required=True)
    parser.add_argument("--champion", type=Path, required=True)
    parser.add_argument("--champion-manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--budgets", default="8,16,32,64")
    parser.add_argument("--positions", type=int, default=100)
    parser.add_argument("--warmup", type=int, default=8)
    parser.add_argument("--leaves-per-batch", type=int, default=4)
    parser.add_argument("--virtual-loss", type=float, default=1.0)
    parser.add_argument("--seed", type=int, default=20260913)
    args = parser.parse_args()
    budgets = [int(value) for value in args.budgets.split(",")]
    if (not budgets or min(budgets) < 2 or args.positions <= 0 or args.warmup < 0 or
            args.leaves_per_batch <= 0 or args.virtual_loss < 0.0):
        raise ValueError("invalid latency benchmark configuration")

    candidate_manifest = verify_model(args.candidate.resolve(),
                                      args.candidate_manifest.resolve())
    champion_manifest = verify_model(args.champion.resolve(),
                                     args.champion_manifest.resolve())
    models = {}
    for name, model, manifest in (
        ("candidate", args.candidate.resolve(), candidate_manifest),
        ("champion", args.champion.resolve(), champion_manifest),
    ):
        result = run_model(
            args.benchmark_exe.resolve(), args.runtime.resolve(), model, budgets,
            args.positions, args.warmup, args.leaves_per_batch,
            args.virtual_loss, args.seed,
        )
        models[name] = {
            "model": str(model), "model_sha256": manifest["onnx_sha256"], **result
        }
    ratios = {
        str(budget): {
            "candidate_over_champion_mean":
                models["candidate"]["measurements"][str(budget)]["mean_ms"] /
                models["champion"]["measurements"][str(budget)]["mean_ms"],
            "candidate_over_champion_p95":
                models["candidate"]["measurements"][str(budget)]["p95_ms"] /
                models["champion"]["measurements"][str(budget)]["p95_ms"],
        }
        for budget in budgets
    }
    report = {
        "report_version": 1, "rule_version": RULE_VERSION,
        "device": "CPUExecutionProvider",
        "scope": "model_load_plus_warm_session_cold_tree_and_warm_tree",
        "parameters": {"budgets": budgets, "positions_per_budget": args.positions,
                       "warmup": args.warmup,
                       "leaves_per_batch": args.leaves_per_batch,
                       "virtual_loss": args.virtual_loss, "seed": args.seed},
        "models": models, "ratios": ratios,
    }
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
