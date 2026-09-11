# MShogi (Mixed Shogi)

**English** | [简体中文](README-CN.md)

MShogi is a two-player desktop board game written in C++17 and Qt 6. It uses a 5-column by 6-row board and combines custom king, rook, bishop, pawn, and promoted-pawn rules with captures, piece drops, promotion, and baseline-entry victories.

## Features

- Canonical turn and board-state management, with Sente starting from the bottom.
- Integrated match setup and odd/even opening draw for player names and clocks.
- Local human-versus-human and human-versus-AI modes with a background Alpha-Beta baseline agent.
- Long non-capturing rook moves, two-square vertical rook assaults, screened bishop captures, and mandatory pawn promotion.
- Canonical per-piece hand cooldown states, legal-move highlighting, and drag-and-drop placement.
- Notation, undo, restart, resignation, pause, and resume controls.
- Per-player clocks, move increments, elapsed match time, and timeout losses.
- Explicit end reasons for king capture, baseline entry, no legal action, threefold-repetition draws, timeout, and resignation.
- A scalable Qt Graphics View interface with QSS, icons, and sound resources.
- Google Test coverage for the board, rules, engine, and UI dragging.

The current rules are documented in MShogi_Rule.md at the managed workspace root.

## Reference Toolchain

- Qt 6.8.3: Core, Gui, Widgets, Multimedia, and Test
- MinGW 13.1.0
- CMake 4.3.2
- Ninja
- C++17

CMake accepts Qt 6.8 or a newer Qt 6 release. Google Test 1.14.0 is loaded offline from the archive committed under tests/.

## Layout

- include/: public headers, the headless GameCore, and the agent interface.
- src/: rules core, Alpha-Beta agent, game engine, clocks, scene, UI, and application entry point.
- tests/: Google Test and Qt Test regression coverage.
- res/ and resources.qrc: style, board texture, button icons, and sounds.
- assets/: README screenshots.

In the managed workspace, this repository lives at ./Src/Mixed-Shogi/, and all generated files belong under ./Src/Build/.

## Build and Test

From the managed workspace root:

    cmake -S ./Src/Mixed-Shogi -B ./Src/Build/MinGW_13_1_0-Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=D:/Apps_D/Qt/6.8.3/mingw_64
    cmake --build ./Src/Build/MinGW_13_1_0-Debug --parallel
    ctest --test-dir ./Src/Build/MinGW_13_1_0-Debug --output-on-failure

For a standalone clone, use an ignored local build directory instead:

    cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=D:/Apps_D/Qt/6.8.3/mingw_64
    cmake --build build --parallel
    ctest --test-dir build --output-on-failure

The application target is MShogiApp; the test target is MShogiTests. For a release-only build, configure with -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF.

Training or self-play hosts can build the headless core without Qt:

    cmake -S . -B build-headless -G Ninja -DMSHOGI_BUILD_APP=OFF -DBUILD_TESTING=OFF
    cmake --build build-headless --parallel

## Self-Play Data

`mshogi_selfplay` writes replayable gzip-compressed JSONL shards:

    ./Src/Build/MinGW_13_1_0-Release/src/mshogi_selfplay.exe --games 100 --output ./Dataset/AI/v1.11.0/selfplay/train_0001.jsonl.gz --seed 20260911 --depth 2 --temperature 1.0 --temperature-plies 12 --max-plies 120

Validate a shard with the `MShogi` Conda environment:

    conda run -n MShogi python ./Src/Mixed-Shogi/tools/inspect_selfplay.py ./Dataset/AI/v1.11.0/selfplay/train_0001.jsonl.gz

The same rules, commit, arguments, and seed produce the same records. Threefold repetition is a rules-level draw recorded as `threefold_repetition`; games reaching `max-plies` are separately recorded as `ply_limit` truncations.

Strict replay uses Python's JSON parser, then checks every state transition with the C++ GameCore:

    conda run -n MShogi python ./Src/Mixed-Shogi/tools/verify_selfplay.py ./Dataset/AI/v1.11.0/selfplay/train_0001.jsonl.gz --replay-exe ./Src/Build/MinGW_13_1_0-Release/src/mshogi_replay_verify.exe

Train the supervised policy-value baseline with ten board planes, hand/cooldown features, rules metadata, a 990-action policy head, and a scalar value head. The default network uses 64 channels and six residual blocks:

    conda run -n MShogi python ./Src/Mixed-Shogi/tools/train_policy_value.py --data "./Dataset/AI/v1.11.0/selfplay/*.jsonl.gz" --output ./Dataset/AI/v1.11.0/models/supervised_1k --epochs 5 --batch-size 256 --device cpu

Games are deterministically split between training, validation, and test sets. Training adds a 180-degree rotated, player-swapped copy; truncated positions are excluded from value loss. Each run keeps model files under `checkpoints/`, metrics and configuration under `reports/`, and event logs under `tensorboard/`.

Compare CPU/GPU forward-only inference throughput without training:

    conda run -n MShogi python ./Src/Mixed-Shogi/tools/benchmark_model.py --checkpoint ./Dataset/AI/v1.11.0/models/supervised_1k/checkpoints/best.pt --devices cpu cuda --output ./Dataset/AI/v1.11.0/models/supervised_1k/benchmarks/cpu_gpu.json

## License

MShogi is licensed under the [GNU General Public License v3](LICENSE).

## Screenshots

![MShogi gameplay](assets/1.png)

![MShogi interface](assets/2.png)
