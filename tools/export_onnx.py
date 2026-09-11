from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
import onnx
import onnxruntime as ort
import torch

from mshogi_ai.data import MShogiDataset, load_shard, sha256_file
from mshogi_ai.model import MShogiNet, ModelConfig


def main() -> int:
    parser = argparse.ArgumentParser(description="Export and verify the Mixed-Shogi ONNX model")
    parser.add_argument("--checkpoint", type=Path, required=True)
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--samples", type=int, default=64)
    args = parser.parse_args()

    checkpoint_path = args.checkpoint.resolve()
    checkpoint = torch.load(checkpoint_path, map_location="cpu", weights_only=False)
    config = ModelConfig(**checkpoint["model_config"])
    model = MShogiNet(config)
    model.load_state_dict(checkpoint["model_state"])
    model.eval()

    output_path = args.output.resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    board = torch.zeros(1, 10, 6, 5)
    hand = torch.zeros(1, 2, 3, 4)
    meta = torch.zeros(1, 5)
    # 动态 batch 让评估器合并多局推理，同时保留桌面单局输入。
    torch.onnx.export(
        model,
        (board, hand, meta),
        output_path,
        input_names=("board", "hand", "meta"),
        output_names=("policy_logits", "value"),
        dynamic_axes={
            "board": {0: "batch"}, "hand": {0: "batch"}, "meta": {0: "batch"},
            "policy_logits": {0: "batch"}, "value": {0: "batch"},
        },
        opset_version=18,
        dynamo=False,
    )
    onnx_model = onnx.load(output_path)
    onnx.checker.check_model(onnx_model)

    shard = load_shard(args.data)
    records = [
        (position, game.truncated)
        for game in shard.games for position in game.positions
    ][:args.samples]
    dataset = MShogiDataset(records)
    boards = torch.stack([dataset[index]["board"] for index in range(len(dataset))])
    hands = torch.stack([dataset[index]["hand"] for index in range(len(dataset))])
    metas = torch.stack([dataset[index]["meta"] for index in range(len(dataset))])
    with torch.inference_mode():
        torch_policy, torch_value = model(boards, hands, metas)

    session = ort.InferenceSession(str(output_path), providers=["CPUExecutionProvider"])
    ort_policy, ort_value = session.run(None, {
        "board": boards.numpy(), "hand": hands.numpy(), "meta": metas.numpy()
    })
    policy_error = float(np.max(np.abs(torch_policy.numpy() - ort_policy)))
    value_error = float(np.max(np.abs(torch_value.numpy() - ort_value)))
    if policy_error > 1.0e-4 or value_error > 1.0e-5:
        raise RuntimeError(
            f"ONNX mismatch: policy={policy_error}, value={value_error}"
        )
    legal_masks = np.stack([
        dataset[index]["legal_mask"].numpy() for index in range(len(dataset))
    ])
    torch_actions = np.where(legal_masks, torch_policy.numpy(), -np.inf).argmax(axis=1)
    ort_actions = np.where(legal_masks, ort_policy, -np.inf).argmax(axis=1)
    action_matches = int(np.count_nonzero(torch_actions == ort_actions))
    if action_matches != len(dataset):
        raise RuntimeError("ONNX legal-masked actions differ from PyTorch")

    manifest = {
        "manifest_version": 1,
        "rule_version": "v1.11.0",
        "action_count": 990,
        "inputs": {"board": ["batch", 10, 6, 5], "hand": ["batch", 2, 3, 4],
                   "meta": ["batch", 5]},
        "outputs": {"policy_logits": ["batch", 990], "value": ["batch"]},
        "opset": 18,
        "checkpoint": str(checkpoint_path),
        "checkpoint_sha256": sha256_file(checkpoint_path),
        "training_commit": checkpoint.get("training_commit", "unknown"),
        "onnx": str(output_path),
        "onnx_sha256": sha256_file(output_path),
        "verification": {
            "samples": len(dataset),
            "providers": session.get_providers(),
            "max_policy_abs_error": policy_error,
            "max_value_abs_error": value_error,
            "legal_masked_action_matches": action_matches,
        },
    }
    manifest_path = args.manifest.resolve()
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2),
                             encoding="utf-8")
    print(json.dumps(manifest, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
