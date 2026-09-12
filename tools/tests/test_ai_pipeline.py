from __future__ import annotations

import gzip
import json

import numpy as np
import torch

from mshogi_ai.data import (
    MShogiDataset,
    PositionRecord,
    TrainingSample,
    parse_state,
    rotate_action_id,
    teacher_policy,
    visit_policy,
)
from mshogi_ai.model import MShogiNet, ModelConfig, policy_value_loss
from run_onnx_arena import promotion_eligible, wilson_interval
from manage_replay_buffer import add_shards
from manage_replay_pool import update_pool
from promote_puct_champion import validate_arena


def sample_state() -> str:
    squares = [".."] * 30
    squares[2] = "GK"
    squares[27] = "SK"
    squares[20] = "SP"
    return (
        "v1.11.0|turn=S|board=" + "".join(squares) +
        "|sHand=P3,R4,|gHand=B1,|flags=10|repetition=2|terminal=0|winner=0"
    )


def test_state_encoding_and_teacher_policy() -> None:
    board, hand, meta = parse_state(sample_state())
    assert board.shape == (10, 6, 5)
    assert board[5, 0, 2] == 1.0
    assert board[0, 5, 2] == 1.0
    assert board[3, 4, 0] == 1.0
    assert hand[0, 2, 2] == 1.0
    assert hand[0, 0, 3] == 1.0
    assert hand[1, 1, 0] == 1.0
    np.testing.assert_allclose(meta, [1.0, 0.0, 1.0, 0.0, 2.0 / 3.0])

    target = teacher_policy(((10, -2.0), (20, 1.0), (30, 100000.0)), 1.0)
    assert np.isclose(target.sum(), 1.0)
    assert target[30] > 0.999
    assert target[20] > 0.0 and target[10] > 0.0
    assert np.count_nonzero(target) == 3


def test_rotation_is_an_involution_and_swaps_players() -> None:
    for action_id in (0, 29, 677, 899, 900, 929, 959, 989):
        assert rotate_action_id(rotate_action_id(action_id)) == action_id

    position = PositionRecord(
        game_id=0,
        ply=0,
        player="S",
        state=sample_state(),
        legal_actions=((677, 0.2), (833, 0.1)),
        selected_action=677,
        outcome=1,
    )
    dataset = MShogiDataset((TrainingSample(position, False, 12, "king_captured"),),
                            augment=True)
    original = dataset[0]
    rotated = dataset[1]
    assert len(dataset) == 2
    assert torch.equal(rotated["meta"][:2], torch.tensor([0.0, 1.0]))
    assert torch.equal(rotated["meta"][2:4], torch.tensor([0.0, 1.0]))
    assert torch.isclose(original["policy_target"].sum().float(), torch.tensor(1.0),
                         atol=1.0e-3)
    assert torch.isclose(rotated["policy_target"].sum().float(), torch.tensor(1.0),
                         atol=1.0e-3)
    assert int(rotated["legal_mask"].sum()) == 2
    assert original["board"].dtype == torch.uint8
    assert original["policy_target"].dtype == torch.float16
    vectorized = dataset.batch((0, 1))
    for name in original:
        assert torch.equal(vectorized[name][0], original[name])
        assert torch.equal(vectorized[name][1], rotated[name])


def test_terminal_distance_weights_and_truncation_mask() -> None:
    position = PositionRecord(0, 4, "S", sample_state(), ((677, 0.2),), 677, 1)
    terminal = MShogiDataset(
        (TrainingSample(position, False, 1, "king_captured"),),
        terminal_value_boost=2.0, terminal_value_decay=8.0, value_discount=0.9,
    )[0]
    truncated = MShogiDataset(
        (TrainingSample(position, True, 9, "ply_limit"),),
        terminal_value_boost=2.0, terminal_value_decay=8.0,
    )[0]
    assert torch.isclose(terminal["value_target"], torch.tensor(1.0))
    assert torch.isclose(terminal["value_weight"], torch.tensor(3.0))
    assert truncated["value_valid"] == 0.0
    assert truncated["value_weight"] == 0.0


def test_puct_visits_are_normalized_without_softmax() -> None:
    target = visit_policy(((10, 1.0), (20, 3.0), (30, 0.0)))
    assert np.isclose(target.sum(), 1.0)
    assert target[10] == 0.25
    assert target[20] == 0.75
    assert target[30] == 0.0

    position = PositionRecord(
        0, 0, "S", sample_state(), ((677, 1.0), (833, 3.0)), 833, 1,
        "puct_visit_counts",
    )
    sample = MShogiDataset((TrainingSample(position, False, 1, "king_captured"),))[0]
    assert torch.isclose(sample["policy_target"][677].float(), torch.tensor(0.25))
    assert torch.isclose(sample["policy_target"][833].float(), torch.tensor(0.75))


def test_policy_value_network_shapes_and_masked_loss() -> None:
    model = MShogiNet(ModelConfig(channels=16, residual_blocks=2))
    board = torch.zeros((3, 10, 6, 5))
    hand = torch.zeros((3, 2, 3, 4))
    meta = torch.zeros((3, 5))
    logits, value = model(board, hand, meta)
    assert logits.shape == (3, 990)
    assert value.shape == (3,)
    assert torch.all(value.abs() <= 1.0)

    legal = torch.zeros((3, 990), dtype=torch.bool)
    legal[:, :2] = True
    policy_target = torch.zeros((3, 990))
    policy_target[:, 0] = 1.0
    loss, policy_loss, value_loss = policy_value_loss(
        logits, value, legal, policy_target, torch.zeros(3), torch.ones(3)
    )
    assert torch.isfinite(loss)
    assert torch.isfinite(policy_loss)
    assert torch.isfinite(value_loss)


def test_wilson_interval_handles_empty_and_extreme_results() -> None:
    assert wilson_interval(0, 0) == [0.0, 0.0]
    lower, upper = wilson_interval(0, 100)
    assert lower == 0.0
    assert 0.03 < upper < 0.04
    lower, upper = wilson_interval(100, 100)
    assert 0.96 < lower < 0.97
    assert upper == 1.0


def test_proof_sample_can_never_promote_champion() -> None:
    assert not promotion_eligible(20, 0, 1.0, 1.0)
    assert not promotion_eligible(1000, 1, 1.0, 1.0)
    assert promotion_eligible(1000, 0, 0.56, 0.51)


def test_versioned_replay_buffer_is_idempotent(tmp_path) -> None:
    model_hash = "A" * 64
    model_manifest = tmp_path / "model.json"
    model_manifest.write_text(json.dumps({"onnx_sha256": model_hash}), encoding="utf-8")
    shard = tmp_path / "puct.jsonl.gz"
    metadata = {
        "record_type": "metadata", "format": "mshogi-selfplay-jsonl-gzip",
        "format_version": 3, "rule_version": "v1.11.0", "action_count": 990,
        "policy_target": "puct_visit_counts", "replay_buffer_version": 1,
        "core_commit": "test", "model_sha256": model_hash, "seed": 7,
        "games": 1, "simulations": 4, "c_puct": 1.5,
        "dirichlet_alpha": 0.3, "dirichlet_epsilon": 0.25,
        "leaves_per_batch": 4, "virtual_loss": 1.0,
    }
    position = {
        "record_type": "position", "game_id": 0, "ply": 0, "player": "S",
        "state": sample_state(), "temperature": 1.0, "selection_seed": 9,
        "root_visits": 4, "tree_reused": False,
        "legal_actions": [[677, 1], [833, 2]], "selected_action": 833,
        "outcome": 1,
    }
    ending = {
        "record_type": "game_end", "game_id": 0, "plies": 1, "winner": 1,
        "end_reason": "king_captured", "truncated": False,
    }
    with gzip.open(shard, "wt", encoding="utf-8") as stream:
        for record in (metadata, position, ending):
            stream.write(json.dumps(record) + "\n")

    first = add_shards(tmp_path / "buffer", [shard], model_manifest)
    second = add_shards(tmp_path / "buffer", [shard], model_manifest)
    assert first["totals"] == {"shards": 1, "games": 1, "positions": 1}
    assert second["totals"] == first["totals"]


def test_replay_pool_evicts_whole_old_generations(tmp_path) -> None:
    def generation(name: str, digest: str, positions: int) -> dict[str, object]:
        return {
            "name": name, "weight": 1.0, "added_at": name,
            "model_sha256": digest, "model_manifest": f"{name}.json",
            "search": {"simulations": 64},
            "shards": [{
                "path": f"{name}.jsonl.gz", "sha256": digest,
                "core_commit": "test", "seed": positions,
                "games": 10, "positions": positions,
            }],
        }

    pool = tmp_path / "pool"
    first = generation("g1", "1" * 64, 80)
    update_pool(pool, first, max_generations=2, max_positions=200)
    idempotent = update_pool(pool, first, max_generations=2, max_positions=200)
    assert idempotent["totals"]["generations"] == 1
    update_pool(pool, generation("g2", "2" * 64, 80), 2, 200)
    final = update_pool(pool, generation("g3", "3" * 64, 80), 2, 200)
    assert [item["name"] for item in final["generations"]] == ["g2", "g3"]
    assert final["totals"]["evicted"] == ["g1"]
    assert sum(item["normalized_weight"] for item in final["generations"]) == 1.0


def test_promotion_evidence_accepts_unordered_games_and_rejects_bad_pairs(tmp_path) -> None:
    games = []
    for pair_id in range(500):
        games.extend([
            {"game_id": pair_id * 2, "pair_id": pair_id,
             "candidate_side": "S", "winner": 1, "truncated": False,
             "candidate_result": "win", "opening_sha256": str(pair_id)},
            {"game_id": pair_id * 2 + 1, "pair_id": pair_id,
             "candidate_side": "G", "winner": 2, "truncated": False,
             "candidate_result": "win", "opening_sha256": str(pair_id)},
        ])
    games.reverse()
    games_path = tmp_path / "games.jsonl"
    games_path.write_text("".join(json.dumps(game) + "\n" for game in games),
                          encoding="utf-8")
    summary = {
        "games": 1000, "pairs": 500,
        "results": {"wins": 1000, "draws": 0, "losses": 0, "truncated": 0,
                    "score_rate": 1.0, "decisive_wilson95": wilson_interval(1000, 1000)},
        "promotion_gate": {"stage": "promotion", "decision": "promote_candidate",
                           "required_games": 1000, "min_score_rate": 0.55,
                           "min_decisive_wilson_lower": 0.50},
    }
    summary_path = tmp_path / "summary.json"
    summary_path.write_text(json.dumps(summary), encoding="utf-8")
    validate_arena(summary_path)
    games[-1]["opening_sha256"] = "tampered"
    games_path.write_text("".join(json.dumps(game) + "\n" for game in games),
                          encoding="utf-8")
    with np.testing.assert_raises(ValueError):
        validate_arena(summary_path)
