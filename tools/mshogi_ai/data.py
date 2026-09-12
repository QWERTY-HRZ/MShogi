from __future__ import annotations

import gzip
import hashlib
import json
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence

import numpy as np
import torch
from torch.utils.data import Dataset

RULE_VERSION = "v1.11.0"
FORMAT_VERSION = 2
ACTION_COUNT = 990
ROWS = 6
COLS = 5
SQUARES = ROWS * COLS

PIECE_CHANNEL = {"K": 0, "R": 1, "B": 2, "P": 3, "H": 4}
HAND_TYPE = {"R": 0, "B": 1, "P": 2}


@dataclass(frozen=True)
class PositionRecord:
    game_id: int
    ply: int
    player: str
    state: str
    legal_actions: tuple[tuple[int, float], ...]
    selected_action: int
    outcome: int


@dataclass(frozen=True)
class GameRecord:
    game_id: int
    positions: tuple[PositionRecord, ...]
    winner: int
    end_reason: str
    truncated: bool


@dataclass(frozen=True)
class Shard:
    path: Path
    metadata: dict[str, object]
    games: tuple[GameRecord, ...]


@dataclass(frozen=True)
class ShardSummary:
    path: Path
    metadata: dict[str, object]
    games: int
    positions: int


@dataclass(frozen=True)
class TrainingSample:
    position: PositionRecord
    truncated: bool
    remaining_plies: int
    end_reason: str


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def load_shard(path: Path | str) -> Shard:
    shard_path = Path(path).resolve()
    metadata: dict[str, object] | None = None
    games: list[GameRecord] = []
    pending: list[PositionRecord] = []
    pending_game_id: int | None = None

    with gzip.open(shard_path, "rt", encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, start=1):
            record = json.loads(line)
            record_type = record.get("record_type")
            if line_number == 1:
                _require(record_type == "metadata", "first record must be metadata")
                metadata = record
                _require(record.get("format") == "mshogi-selfplay-jsonl-gzip",
                         "unexpected data format")
                _require(int(record.get("format_version", -1)) == FORMAT_VERSION,
                         "training requires self-play format v2")
                _require(record.get("rule_version") == RULE_VERSION,
                         "training requires rule v1.11.0")
                _require(int(record.get("action_count", -1)) == ACTION_COUNT,
                         "unexpected action count")
                continue

            _require(metadata is not None, "metadata is missing")
            if record_type == "position":
                game_id = int(record["game_id"])
                if pending_game_id is None:
                    pending_game_id = game_id
                _require(game_id == pending_game_id,
                         f"interleaved games at line {line_number}")
                ply = int(record["ply"])
                _require(ply == len(pending), f"non-contiguous ply at line {line_number}")
                player = str(record["player"])
                _require(player == ("S" if ply % 2 == 0 else "G"),
                         f"unexpected player at line {line_number}")
                state = str(record["state"])
                _require(state.startswith(f"{RULE_VERSION}|"),
                         f"state version mismatch at line {line_number}")
                _require("|repetition=" in state,
                         f"repetition feature missing at line {line_number}")

                actions = tuple((int(item[0]), float(item[1]))
                                for item in record["legal_actions"])
                action_ids = [action_id for action_id, _ in actions]
                _require(bool(actions), f"empty legal action list at line {line_number}")
                _require(len(action_ids) == len(set(action_ids)),
                         f"duplicate legal action at line {line_number}")
                _require(all(0 <= action_id < ACTION_COUNT for action_id in action_ids),
                         f"action id out of range at line {line_number}")
                _require(all(math.isfinite(score) for _, score in actions),
                         f"non-finite teacher score at line {line_number}")
                selected_action = int(record["selected_action"])
                _require(selected_action in set(action_ids),
                         f"selected action is illegal at line {line_number}")
                outcome = int(record["outcome"])
                _require(outcome in (-1, 0, 1),
                         f"invalid outcome at line {line_number}")
                pending.append(PositionRecord(game_id, ply, player, state, actions,
                                              selected_action, outcome))
                continue

            if record_type == "game_end":
                game_id = int(record["game_id"])
                _require(pending_game_id == game_id,
                         f"game end mismatch at line {line_number}")
                _require(int(record["plies"]) == len(pending),
                         f"game length mismatch at line {line_number}")
                winner = int(record["winner"])
                reason = str(record["end_reason"])
                _require(isinstance(record["truncated"], bool),
                         f"truncated must be boolean at line {line_number}")
                truncated = record["truncated"]
                _require(winner in (0, 1, 2), f"invalid winner at line {line_number}")
                if truncated:
                    _require(winner == 0 and reason == "ply_limit",
                             f"invalid truncation at line {line_number}")
                elif reason == "threefold_repetition":
                    _require(winner == 0, f"draw has a winner at line {line_number}")
                else:
                    _require(winner in (1, 2) and reason in {
                        "king_captured", "baseline_entry", "no_legal_action"
                    }, f"invalid decisive result at line {line_number}")

                for position in pending:
                    expected_outcome = 0 if winner == 0 else (
                        1 if winner == (1 if position.player == "S" else 2) else -1
                    )
                    _require(position.outcome == expected_outcome,
                             f"outcome mismatch in game {game_id}, ply {position.ply}")
                games.append(GameRecord(game_id, tuple(pending), winner, reason, truncated))
                pending = []
                pending_game_id = None
                continue

            raise ValueError(f"unknown record type at line {line_number}")

    _require(metadata is not None, "empty shard")
    _require(pending_game_id is None, "unterminated final game")
    _require(len(games) == int(metadata["games"]), "metadata game count mismatch")
    _require([game.game_id for game in games] == list(range(len(games))),
             "game ids must be contiguous from zero")
    return Shard(shard_path, metadata, tuple(games))


def _game_identity(shard: Shard, game_id: int) -> str:
    return f"{shard.metadata['core_commit']}:{shard.metadata['seed']}:{game_id}"


def _split_name(identity: str, seed: int) -> str:
    key = f"{identity}:{seed}".encode("utf-8")
    bucket = int.from_bytes(hashlib.sha256(key).digest()[:8], "big") % 10
    if bucket < 8:
        return "train"
    return "validation" if bucket == 8 else "test"


def load_split_samples(
    paths: Iterable[Path | str], split_seed: int = 20260911
) -> tuple[
    dict[str, list[TrainingSample]],
    list[ShardSummary],
    dict[str, int],
]:
    splits: dict[str, list[TrainingSample]] = {
        "train": [], "validation": [], "test": []
    }
    game_counts = {"train": 0, "validation": 0, "test": 0}
    shards: list[ShardSummary] = []
    identities: set[str] = set()
    for path in sorted((Path(item) for item in paths), key=lambda item: str(item)):
        shard = load_shard(path)
        shards.append(ShardSummary(
            shard.path, shard.metadata, len(shard.games),
            sum(len(game.positions) for game in shard.games)
        ))
        for game in shard.games:
            identity = _game_identity(shard, game.game_id)
            _require(identity not in identities, f"duplicate game identity: {identity}")
            identities.add(identity)
            split = _split_name(identity, split_seed)
            game_counts[split] += 1
            splits[split].extend(
                TrainingSample(position, game.truncated,
                               len(game.positions) - position.ply, game.end_reason)
                for position in game.positions
            )
        # 只保留清单摘要，避免 20k 训练时重复持有完整对局树。
        del shard
    return splits, shards, game_counts


def parse_state(state: str) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    parts = state.split("|")
    _require(parts[0] == RULE_VERSION, "unexpected state version")
    fields = dict(part.split("=", 1) for part in parts[1:])
    board_text = fields["board"]
    _require(len(board_text) == SQUARES * 2, "board encoding must contain 30 squares")

    board = np.zeros((10, ROWS, COLS), dtype=np.float32)
    for square in range(SQUARES):
        token = board_text[square * 2:square * 2 + 2]
        if token == "..":
            continue
        _require(token[0] in ("S", "G") and token[1] in PIECE_CHANNEL,
                 f"invalid board token {token}")
        channel = PIECE_CHANNEL[token[1]] + (0 if token[0] == "S" else 5)
        board[channel, square // COLS, square % COLS] = 1.0

    hand = np.zeros((2, 3, 4), dtype=np.float32)
    for owner_index, field_name in enumerate(("sHand", "gHand")):
        for token in filter(None, fields[field_name].split(",")):
            _require(token[0] in HAND_TYPE, f"invalid hand token {token}")
            cooldown = int(token[1:])
            _require(1 <= cooldown <= 4, f"invalid hand cooldown {cooldown}")
            hand[owner_index, HAND_TYPE[token[0]], cooldown - 1] += 1.0

    flags = fields["flags"]
    _require(len(flags) == 2 and set(flags) <= {"0", "1"}, "invalid baseline flags")
    repetition = int(fields["repetition"])
    _require(1 <= repetition <= 3, "invalid repetition count")
    meta = np.asarray([
        1.0 if fields["turn"] == "S" else 0.0,
        1.0 if fields["turn"] == "G" else 0.0,
        float(flags[0]),
        float(flags[1]),
        repetition / 3.0,
    ], dtype=np.float32)
    _require(meta[0] + meta[1] == 1.0, "invalid current player")
    _require(fields["terminal"] == "0" and fields["winner"] == "0",
             "training positions must be non-terminal")
    return board, hand, meta


def teacher_policy(actions: Sequence[tuple[int, float]], temperature: float) -> np.ndarray:
    _require(temperature > 0.0, "teacher temperature must be positive")
    target = np.zeros(ACTION_COUNT, dtype=np.float32)
    ids = np.asarray([item[0] for item in actions], dtype=np.int64)
    scores = np.asarray([item[1] for item in actions], dtype=np.float64)
    # 减最大值并裁剪尾部，既避免溢出，也保留明显制胜着的一峰标签。
    logits = np.clip((scores - scores.max()) / temperature, -20.0, 0.0)
    probabilities = np.exp(logits)
    probabilities /= probabilities.sum()
    target[ids] = probabilities.astype(np.float32)
    return target


def rotate_action_id(action_id: int) -> int:
    _require(0 <= action_id < ACTION_COUNT, "action id out of range")
    if action_id < 900:
        from_square, to_square = divmod(action_id, SQUARES)
        return (SQUARES - 1 - from_square) * SQUARES + (SQUARES - 1 - to_square)
    drop_offset = action_id - 900
    drop_type, to_square = divmod(drop_offset, SQUARES)
    return 900 + drop_type * SQUARES + (SQUARES - 1 - to_square)


ROTATED_ACTION_IDS = np.asarray(
    [rotate_action_id(action_id) for action_id in range(ACTION_COUNT)], dtype=np.int64
)


class MShogiDataset(Dataset[dict[str, torch.Tensor]]):
    def __init__(
        self,
        samples: Sequence[TrainingSample],
        teacher_temperature: float = 1.0,
        augment: bool = False,
        terminal_value_boost: float = 0.0,
        terminal_value_decay: float = 8.0,
        value_discount: float = 1.0,
    ) -> None:
        _require(terminal_value_boost >= 0.0, "terminal value boost must be non-negative")
        _require(terminal_value_decay > 0.0, "terminal value decay must be positive")
        _require(0.0 < value_discount <= 1.0, "value discount must be in (0, 1]")
        self.augment = augment
        count = len(samples)
        self.boards = np.zeros((count, 10, ROWS, COLS), dtype=np.uint8)
        self.hands = np.zeros((count, 2, 3, 4), dtype=np.uint8)
        self.metas = np.zeros((count, 5), dtype=np.float16)
        self.legal_masks = np.zeros((count, ACTION_COUNT), dtype=np.bool_)
        self.policy_targets = np.zeros((count, ACTION_COUNT), dtype=np.float16)
        self.value_targets = np.zeros((count,), dtype=np.float32)
        self.value_weights = np.ones((count,), dtype=np.float32)
        self.value_valid = np.ones((count,), dtype=np.float32)
        self.plies = np.zeros((count,), dtype=np.int16)
        self.remaining_plies = np.zeros((count,), dtype=np.int16)

        for index, sample in enumerate(samples):
            position = sample.position
            board, hand, meta = parse_state(position.state)
            self.boards[index] = board
            self.hands[index] = hand
            self.metas[index] = meta
            ids = [action_id for action_id, _ in position.legal_actions]
            self.legal_masks[index, ids] = True
            self.policy_targets[index] = teacher_policy(
                position.legal_actions, teacher_temperature
            )
            distance = max(0, sample.remaining_plies - 1)
            self.value_targets[index] = float(position.outcome) * value_discount ** distance
            # 截断不是规则和棋，不用伪标签训练价值头。
            self.value_valid[index] = 0.0 if sample.truncated else 1.0
            self.value_weights[index] = self.value_valid[index] * (
                1.0 + terminal_value_boost * math.exp(-distance / terminal_value_decay)
            )
            self.plies[index] = position.ply
            self.remaining_plies[index] = sample.remaining_plies

    def __len__(self) -> int:
        return len(self.boards) * (2 if self.augment else 1)

    def __getitem__(self, index: int) -> dict[str, torch.Tensor]:
        base_count = len(self.boards)
        rotated = self.augment and index >= base_count
        base_index = index % base_count
        board = self.boards[base_index]
        hand = self.hands[base_index]
        meta = self.metas[base_index]
        legal = self.legal_masks[base_index]
        policy = self.policy_targets[base_index]

        if rotated:
            spatial = np.flip(board, axis=(1, 2))
            board = np.concatenate((spatial[5:], spatial[:5]), axis=0).copy()
            hand = hand[::-1].copy()
            meta = meta[[1, 0, 3, 2, 4]].copy()
            rotated_legal = np.zeros_like(legal)
            rotated_policy = np.zeros_like(policy)
            rotated_legal[ROTATED_ACTION_IDS] = legal
            rotated_policy[ROTATED_ACTION_IDS] = policy
            legal = rotated_legal
            policy = rotated_policy

        return {
            "board": torch.from_numpy(np.asarray(board)),
            "hand": torch.from_numpy(np.asarray(hand)),
            "meta": torch.from_numpy(np.asarray(meta)),
            "legal_mask": torch.from_numpy(np.asarray(legal)),
            "policy_target": torch.from_numpy(np.asarray(policy)),
            "value_target": torch.tensor(self.value_targets[base_index]),
            "value_weight": torch.tensor(self.value_weights[base_index]),
            "value_valid": torch.tensor(self.value_valid[base_index]),
            "ply": torch.tensor(self.plies[base_index], dtype=torch.int32),
            "remaining_plies": torch.tensor(
                self.remaining_plies[base_index], dtype=torch.int32
            ),
        }


def sha256_file(path: Path | str) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()
