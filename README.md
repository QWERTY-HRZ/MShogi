# MShogi (Mixed Shogi)

**English** | [简体中文](README-CN.md)

MShogi is a two-player desktop board game written in C++17 and Qt 6. It uses a 5-column by 6-row board and combines custom king, rook, bishop, pawn, and promoted-pawn rules with captures, piece drops, promotion, and baseline-entry victories.

## Features

- Canonical turn and board-state management, with Sente starting from the bottom.
- Integrated match setup and odd/even opening draw for player names and clocks.
- Local human-versus-human and human-versus-AI modes with a background Alpha-Beta baseline agent.
- Long non-capturing rook moves, two-square vertical rook assaults, screened bishop captures, and mandatory pawn promotion.
- Per-piece hand cooldowns, legal-move highlighting, and drag-and-drop placement.
- Notation, undo, restart, resignation, pause, and resume controls.
- Per-player clocks, move increments, elapsed match time, and timeout losses.
- Explicit end reasons for king capture, baseline entry, no legal action, timeout, and resignation.
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

## License

MShogi is licensed under the [GNU General Public License v3](LICENSE).

## Screenshots

![MShogi gameplay](assets/1.png)

![MShogi interface](assets/2.png)
