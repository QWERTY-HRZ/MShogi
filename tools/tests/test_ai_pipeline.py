from __future__ import annotations

import numpy as np
import torch

from mshogi_ai.data import (
    MShogiDataset,
    PositionRecord,
    TrainingSample,
    parse_state,
    rotate_action_id,
    teacher_policy,
)
from mshogi_ai.model import MShogiNet, ModelConfig, policy_value_loss
from run_onnx_arena import wilson_interval


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
