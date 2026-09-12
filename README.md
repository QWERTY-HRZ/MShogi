# MShogi (Mixed Shogi)

**English** | [简体中文](README-CN.md)

MShogi is a two-player desktop board game written in C++17 and Qt 6. It uses a 5-column by 6-row board and combines custom king, rook, bishop, pawn, and promoted-pawn rules with captures, piece drops, promotion, and baseline-entry victories.

## Features

- Canonical turn and board-state management, with Sente starting from the bottom.
- Integrated match setup and odd/even opening draw for player names and clocks.
- Local human-versus-human and human-versus-AI modes with a deployed 20k policy-value agent and an Alpha-Beta fallback option.
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

Export dynamic-batch ONNX and verify numerical and legal-masked action equivalence:

    conda run -n MShogi python ./Src/Mixed-Shogi/tools/export_onnx.py --checkpoint ./Dataset/AI/v1.11.0/models/supervised_1k/checkpoints/best.pt --data ./Dataset/AI/v1.11.0/selfplay/supervised_1k_seed_20260911.jsonl.gz --output ./Dataset/AI/v1.11.0/models/supervised_1k/onnx/mshogi_policy_value.onnx --manifest ./Dataset/AI/v1.11.0/models/supervised_1k/onnx/manifest.json

`mshogi_arena` keeps rules and multithreaded Alpha-Beta in the C++ GameCore while Python batches neural positions through ONNX Runtime. Paired games share an opening and swap model sides. The 1k baseline failed its gate; the 20k model passed with a 63.80% direct-policy score and a 60.80% decisive-game Wilson lower bound against depth 1, so it is exposed in the desktop client.

Use the value head to rerank the policy's top-five candidates:

    conda run -n MShogi python ./Src/Mixed-Shogi/tools/run_onnx_arena.py --arena-exe ./Src/Build/MinGW_13_1_0-Release/src/mshogi_arena.exe --model ./Dataset/AI/v1.11.0/models/supervised_20k/iter1_a/onnx/mshogi_policy_value.onnx --manifest ./Dataset/AI/v1.11.0/models/supervised_20k/iter1_a/onnx/manifest.json --output ./Dataset/AI/v1.11.0/models/supervised_20k/iter1_a/evaluation/rerank_ab_depth_1_1k --games 1000 --depth 1 --top-k 5 --policy-weight 1 --value-weight 0.5

Policy candidates are expanded by the C++ GameCore. Immediate terminal actions use exact outcomes, while non-terminal child positions are batch-evaluated by the value head. The deployed 20k model scores 77.30% with this configuration against depth 1.

Build and package the optional Windows neural client by providing ONNX Runtime 1.29 headers/DLL and the verified model:

    cmake -S ./Src/Mixed-Shogi -B ./Src/Build/MinGW_13_1_0-Release -DMSHOGI_ENABLE_ONNX_AGENT=ON -DMSHOGI_ONNXRUNTIME_ROOT=./Src/Build/ThirdParty/onnxruntime-1.29.0 -DMSHOGI_ONNX_MODEL=./Dataset/AI/v1.11.0/models/supervised_20k/iter1_a/onnx/mshogi_policy_value.onnx
    cmake --build ./Src/Build/MinGW_13_1_0-Release --parallel
    ./Src/Mixed-Shogi/tools/package_windows.ps1 -BuildDirectory ./Src/Build/MinGW_13_1_0-Release -OutputDirectory ./Src/Build/MShogi-v1.11.0-Neural-Release -QtRoot D:/Apps_D/Qt/6.8.3/mingw_64 -OnnxRuntimeDll ./Src/Build/ThirdParty/onnxruntime-1.29.0/onnxruntime.dll -OnnxModel ./Dataset/AI/v1.11.0/models/supervised_20k/iter1_a/onnx/mshogi_policy_value.onnx

The package includes CPU ONNX Runtime, the model, Qt/MinGW/MSVC runtime files, and a SHA-256 deployment manifest. It requires neither Python nor CUDA. Run `MShogiApp.exe --ai-smoke` for a non-interactive model deployment check.

## License

MShogi is licensed under the [GNU General Public License v3](LICENSE).

## Screenshots

![MShogi gameplay](assets/1.png)

![MShogi interface](assets/2.png)
