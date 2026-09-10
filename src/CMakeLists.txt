set(MSHOGI_LIB_SOURCES
    GameEngine.cpp
    UIController.cpp
    MoveHistory.cpp
    PieceItem.cpp
    GameScene.cpp
    ChessClock.cpp
    OpeningDraw.cpp
    GameSetupDialog.cpp
)

set(MSHOGI_LIB_HEADERS
    ../include/GameEngine.h
    ../include/UIController.h
    ../include/Piece.h
    ../include/Board.h
    ../include/RuleEngine.h
    ../include/MoveHistory.h
    ../include/PieceItem.h
    ../include/GameScene.h
    ../include/GameConstants.h
    ../include/ChessClock.h
    ../include/OpeningDraw.h
    ../include/GameSetupDialog.h
)

add_library(MShogiCore STATIC
    Board.cpp
    RuleEngine.cpp
    GameCore.cpp
    GameAgent.cpp
    ../include/Piece.h
    ../include/Board.h
    ../include/RuleEngine.h
    ../include/GameCore.h
    ../include/GameAgent.h
    ../include/GameConstants.h
)

target_include_directories(MShogiCore PUBLIC ${PROJECT_SOURCE_DIR}/include)

if(NOT MSHOGI_BUILD_APP)
    return()
endif()

set(MSHOGI_RESOURCES ../resources.qrc)

add_library(MShogiLib STATIC
    ${MSHOGI_LIB_SOURCES}
    ${MSHOGI_LIB_HEADERS}
    ${MSHOGI_RESOURCES}
)

target_include_directories(MShogiLib PUBLIC ${PROJECT_SOURCE_DIR}/include)

target_link_libraries(MShogiLib PUBLIC
    MShogiCore
    Qt6::Core
    Qt6::Widgets
    Qt6::Gui
    Qt6::Multimedia
    Qt6::Concurrent
)

add_executable(MShogiApp
    main.cpp
    ${MSHOGI_RESOURCES}
)

target_link_libraries(MShogiApp PRIVATE
    MShogiLib
    Qt6::Core
    Qt6::Widgets
    Qt6::Gui
    Qt6::Multimedia
)
