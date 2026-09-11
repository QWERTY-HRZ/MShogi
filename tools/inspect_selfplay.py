from __future__ import annotations

import argparse
import gzip
import json
from collections import Counter
from pathlib import Path


def inspect(path: Path) -> dict[str, object]:
    counts: Counter[str] = Counter()
    winners: Counter[int] = Counter()
    metadata: dict[str, object] | None = None
    game_ids: set[int] = set()
    draws = 0
    truncated_games = 0

    with gzip.open(path, "rt", encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, start=1):
            record = json.loads(line)
            record_type = record.get("record_type")
            counts[record_type] += 1

            if line_number == 1:
                if record_type != "metadata":
                    raise ValueError("first record must be metadata")
                metadata = record
                version_pair = (int(record.get("format_version", -1)), record.get("rule_version"))
                if version_pair not in {(1, "v1.10.0"), (2, "v1.11.0")}:
                    raise ValueError("unsupported format/rule version pair")
                if int(record.get("action_count", -1)) != 990:
                    raise ValueError("unexpected action count")
                continue

            if record_type == "position":
                state = str(record.get("state", ""))
                if not state.startswith(f"{metadata['rule_version']}|"):
                    raise ValueError(f"state/rule version mismatch at line {line_number}")
                if int(metadata["format_version"]) >= 2 and "|repetition=" not in state:
                    raise ValueError(f"repetition state is missing at line {line_number}")
                legal_actions = record.get("legal_actions", [])
                legal_ids = {int(action[0]) for action in legal_actions}
                if not legal_ids or any(action_id < 0 or action_id >= 990 for action_id in legal_ids):
                    raise ValueError(f"invalid legal action list at line {line_number}")
                if int(record["selected_action"]) not in legal_ids:
                    raise ValueError(f"selected action is not legal at line {line_number}")
                if int(record["outcome"]) not in (-1, 0, 1):
                    raise ValueError(f"invalid outcome at line {line_number}")
                if float(record["temperature"]) < 0 or int(record["selection_seed"]) < 0:
                    raise ValueError(f"invalid selection metadata at line {line_number}")
                game_ids.add(int(record["game_id"]))
            elif record_type == "game_end":
                winner = int(record["winner"])
                if winner not in (0, 1, 2):
                    raise ValueError(f"invalid winner at line {line_number}")
                truncated = bool(record.get("truncated"))
                end_reason = record.get("end_reason")
                decisive_reasons = {"king_captured", "baseline_entry", "no_legal_action"}
                if truncated:
                    if winner != 0 or end_reason != "ply_limit":
                        raise ValueError(f"invalid truncated result at line {line_number}")
                    truncated_games += 1
                elif end_reason == "threefold_repetition":
                    if winner != 0:
                        raise ValueError(f"repetition draw has a winner at line {line_number}")
                    draws += 1
                elif winner == 0:
                    raise ValueError(f"non-truncated zero-winner game is not a draw at line {line_number}")
                elif end_reason not in decisive_reasons:
                    raise ValueError(f"winner has invalid end reason at line {line_number}")
                winners[winner] += 1
                game_ids.add(int(record["game_id"]))
            else:
                raise ValueError(f"unknown record type at line {line_number}")

    if metadata is None:
        raise ValueError("metadata is missing")
    if counts["game_end"] != int(metadata["games"]):
        raise ValueError("game count does not match metadata")

    return {
        "path": str(path.resolve()),
        "compressed_bytes": path.stat().st_size,
        "format_version": metadata["format_version"],
        "rule_version": metadata["rule_version"],
        "core_commit": metadata["core_commit"],
        "seed": metadata["seed"],
        "games": counts["game_end"],
        "positions": counts["position"],
        "sente_wins": winners[1],
        "gote_wins": winners[2],
        "draws": draws,
        "truncated": truncated_games,
        "unique_game_ids": len(game_ids),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate a Mixed-Shogi self-play shard")
    parser.add_argument("path", type=Path)
    args = parser.parse_args()
    summary = inspect(args.path)
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
