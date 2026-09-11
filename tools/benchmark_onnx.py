from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

import numpy as np
import onnxruntime as ort

from mshogi_ai.data import sha256_file


def main() -> int:
    parser = argparse.ArgumentParser(description="Benchmark ONNX Runtime inference")
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--batch-sizes", nargs="+", type=int, default=(1, 64, 256, 1024))
    parser.add_argument("--thread-counts", nargs="+", type=int, default=(1, 4, 16))
    parser.add_argument("--warmup", type=int, default=50)
    parser.add_argument("--iterations", type=int, default=200)
    args = parser.parse_args()

    model = args.model.resolve()
    rng = np.random.default_rng(20260912)
    results: dict[str, object] = {
        "model": str(model), "model_sha256": sha256_file(model),
        "onnxruntime": ort.__version__, "provider": "CPUExecutionProvider",
        "warmup": args.warmup, "iterations": args.iterations, "runs": [],
    }
    for threads in args.thread_counts:
        options = ort.SessionOptions()
        options.intra_op_num_threads = threads
        options.inter_op_num_threads = 1
        options.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
        session = ort.InferenceSession(
            str(model), sess_options=options, providers=["CPUExecutionProvider"]
        )
        for batch_size in args.batch_sizes:
            inputs = {
                "board": rng.standard_normal((batch_size, 10, 6, 5), dtype=np.float32),
                "hand": rng.standard_normal((batch_size, 2, 3, 4), dtype=np.float32),
                "meta": rng.standard_normal((batch_size, 5), dtype=np.float32),
            }
            for _ in range(args.warmup):
                session.run(None, inputs)
            started = time.perf_counter()
            for _ in range(args.iterations):
                session.run(None, inputs)
            elapsed = time.perf_counter() - started
            results["runs"].append({
                "threads": threads, "batch_size": batch_size,
                "batch_ms": elapsed * 1000.0 / args.iterations,
                "positions_per_second": batch_size * args.iterations / elapsed,
            })

    rendered = json.dumps(results, ensure_ascii=False, indent=2)
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(rendered, encoding="utf-8")
    print(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
