from __future__ import annotations

import argparse
import io
import json
import subprocess
from pathlib import Path

from mshogi_ai.data import load_shard, sha256_file


def default_replay_executable() -> Path:
    repository = Path(__file__).resolve().parents[1]
    build_root = repository.parent / "Build"
    candidates = [
        build_root / "MinGW_13_1_0-Release" / "src" / "mshogi_replay_verify.exe",
        build_root / "MinGW_13_1_0-Debug" / "src" / "mshogi_replay_verify.exe",
        build_root / "Headless-Release" / "src" / "mshogi_replay_verify.exe",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    raise FileNotFoundError("mshogi_replay_verify executable was not found")


def verify(path: Path, executable: Path) -> dict[str, object]:
    shard = load_shard(path)
    protocol = io.StringIO()
    position_count = 0
    for game in shard.games:
        protocol.write(f"G\t{game.game_id}\n")
        for position in game.positions:
            protocol.write(
                f"P\t{game.game_id}\t{position.ply}\t{position.player}\t"
                f"{position.selected_action}\t{position.state}\n"
            )
            position_count += 1
        protocol.write(
            f"E\t{game.game_id}\t{len(game.positions)}\t{game.winner}\t"
            f"{game.end_reason}\t{int(game.truncated)}\n"
        )

    # Python 解析 JSON，C++ 核心只接收已结构化协议并逐步重放规则。
    result = subprocess.run(
        [str(executable)],
        input=protocol.getvalue(),
        text=True,
        encoding="utf-8",
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "C++ replay verification failed")
    return {
        "path": str(shard.path),
        "sha256": sha256_file(shard.path),
        "games": len(shard.games),
        "positions": position_count,
        "core_result": result.stdout.strip(),
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Structurally validate and replay Mixed-Shogi self-play shards"
    )
    parser.add_argument("paths", nargs="+", type=Path)
    parser.add_argument("--replay-exe", type=Path)
    args = parser.parse_args()
    executable = (args.replay_exe or default_replay_executable()).resolve()
    summaries = [verify(path, executable) for path in args.paths]
    print(json.dumps({"replay_executable": str(executable), "shards": summaries},
                     ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
