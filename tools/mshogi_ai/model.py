from __future__ import annotations

from dataclasses import asdict, dataclass

import torch
from torch import nn
from torch.nn import functional as F

from .data import ACTION_COUNT, COLS, ROWS


@dataclass(frozen=True)
class ModelConfig:
    channels: int = 64
    residual_blocks: int = 6
    policy_channels: int = 16
    value_channels: int = 8
    action_count: int = ACTION_COUNT

    def to_dict(self) -> dict[str, int]:
        return asdict(self)


class ResidualBlock(nn.Module):
    def __init__(self, channels: int) -> None:
        super().__init__()
        self.conv1 = nn.Conv2d(channels, channels, kernel_size=3, padding=1, bias=False)
        self.bn1 = nn.BatchNorm2d(channels)
        self.conv2 = nn.Conv2d(channels, channels, kernel_size=3, padding=1, bias=False)
        self.bn2 = nn.BatchNorm2d(channels)

    def forward(self, inputs: torch.Tensor) -> torch.Tensor:
        hidden = F.relu(self.bn1(self.conv1(inputs)))
        return F.relu(inputs + self.bn2(self.conv2(hidden)))


class MShogiNet(nn.Module):
    def __init__(self, config: ModelConfig = ModelConfig()) -> None:
        super().__init__()
        self.config = config
        self.stem = nn.Conv2d(10, config.channels, kernel_size=3, padding=1, bias=False)
        self.stem_bn = nn.BatchNorm2d(config.channels)
        self.auxiliary = nn.Linear(2 * 3 * 4 + 5, config.channels)
        self.blocks = nn.Sequential(*[
            ResidualBlock(config.channels) for _ in range(config.residual_blocks)
        ])

        self.policy_conv = nn.Conv2d(config.channels, config.policy_channels, 1, bias=False)
        self.policy_bn = nn.BatchNorm2d(config.policy_channels)
        self.policy_fc = nn.Linear(
            config.policy_channels * ROWS * COLS, config.action_count
        )

        self.value_conv = nn.Conv2d(config.channels, config.value_channels, 1, bias=False)
        self.value_bn = nn.BatchNorm2d(config.value_channels)
        self.value_fc1 = nn.Linear(config.value_channels * ROWS * COLS, config.channels)
        self.value_fc2 = nn.Linear(config.channels, 1)

    def forward(
        self, board: torch.Tensor, hand: torch.Tensor, meta: torch.Tensor
    ) -> tuple[torch.Tensor, torch.Tensor]:
        auxiliary = torch.cat((hand.flatten(1), meta), dim=1)
        # 手驹与规则元数据投影后广播到每个棋盘格，与空间特征共同搜索。
        hidden = self.stem_bn(self.stem(board))
        hidden = F.relu(hidden + self.auxiliary(auxiliary).unsqueeze(-1).unsqueeze(-1))
        hidden = self.blocks(hidden)

        policy = F.relu(self.policy_bn(self.policy_conv(hidden))).flatten(1)
        policy_logits = self.policy_fc(policy)

        value = F.relu(self.value_bn(self.value_conv(hidden))).flatten(1)
        value = torch.tanh(self.value_fc2(F.relu(self.value_fc1(value)))).squeeze(1)
        return policy_logits, value


def policy_value_loss(
    policy_logits: torch.Tensor,
    value: torch.Tensor,
    legal_mask: torch.Tensor,
    policy_target: torch.Tensor,
    value_target: torch.Tensor,
    value_sample_weight: torch.Tensor,
    value_weight: float = 1.0,
) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor]:
    masked_logits = policy_logits.masked_fill(~legal_mask, -1.0e9)
    policy_loss = -(policy_target * F.log_softmax(masked_logits, dim=1)).sum(dim=1).mean()
    value_errors = (value - value_target).square() * value_sample_weight
    value_loss = value_errors.sum() / value_sample_weight.sum().clamp_min(1.0)
    return policy_loss + value_weight * value_loss, policy_loss, value_loss


def count_parameters(model: nn.Module) -> int:
    return sum(parameter.numel() for parameter in model.parameters())


def expand_model_state(
    source_state: dict[str, torch.Tensor],
    source_config: ModelConfig,
    target_config: ModelConfig,
) -> dict[str, torch.Tensor]:
    if (target_config.channels < source_config.channels or
            target_config.residual_blocks < source_config.residual_blocks or
            target_config.policy_channels != source_config.policy_channels or
            target_config.value_channels != source_config.value_channels or
            target_config.action_count != source_config.action_count):
        raise ValueError("target model is not a compatible width/depth expansion")
    target = MShogiNet(target_config).state_dict()
    random_extra_prefixes = ("stem.", "stem_bn.", "auxiliary.", "value_fc1.")
    for name, source in source_state.items():
        if name not in target or source.ndim != target[name].ndim or any(
                source_size > target_size for source_size, target_size in
                zip(source.shape, target[name].shape, strict=True)):
            raise ValueError(f"cannot expand checkpoint tensor {name}")
        if source.shape != target[name].shape and not name.startswith(random_extra_prefixes):
            if target[name].is_floating_point():
                target[name].fill_(1.0 if name.endswith("running_var") else 0.0)
            else:
                target[name].zero_()
        slices = tuple(slice(0, size) for size in source.shape)
        target[name][slices].copy_(source)
    for block_index in range(source_config.residual_blocks,
                             target_config.residual_blocks):
        # 新块第一层保留随机特征，第二层从零开始，初始为恒等且梯度可进入。
        target[f"blocks.{block_index}.conv2.weight"].zero_()
    return target
