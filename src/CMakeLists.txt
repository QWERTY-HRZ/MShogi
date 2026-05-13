set(MSHOGI_LIB_SOURCES
    GameEngine.cpp
    UIController.cpp
    Board.cpp
    RuleEngine.cpp
    MoveHistory.cpp
    PieceItem.cpp
    GameScene.cpp
    ChessClock.cpp
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
)

set(MSHOGI_RESOURCES ../resources.qrc)

add_library(MShogiLib STATIC
    ${MSHOGI_LIB_SOURCES}
    ${MSHOGI_LIB_HEADERS}
    ${MSHOGI_RESOURCES}
)

target_include_directories(MShogiLib PUBLIC ../include)

target_link_libraries(MShogiLib PUBLIC
    Qt6::Core
    Qt6::Widgets
    Qt6::Gui
    Qt6::Multimedia
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
