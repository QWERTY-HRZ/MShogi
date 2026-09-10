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

if(MINGW)
    get_filename_component(MSHOGI_MINGW_BIN_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)
    get_filename_component(MSHOGI_MINGW_ROOT ${MSHOGI_MINGW_BIN_DIR} DIRECTORY)
    set(MSHOGI_ZLIB_INCLUDE_DIR ${MSHOGI_MINGW_ROOT}/x86_64-w64-mingw32/include)
    find_library(MSHOGI_ZLIB_LIBRARY NAMES z
        PATHS ${MSHOGI_MINGW_ROOT}/x86_64-w64-mingw32/lib
        NO_DEFAULT_PATH
        REQUIRED
    )
    add_library(MShogiZlib UNKNOWN IMPORTED)
    set_target_properties(MShogiZlib PROPERTIES
        IMPORTED_LOCATION ${MSHOGI_ZLIB_LIBRARY}
        INTERFACE_INCLUDE_DIRECTORIES ${MSHOGI_ZLIB_INCLUDE_DIR}
    )
else()
    find_package(ZLIB REQUIRED)
    add_library(MShogiZlib ALIAS ZLIB::ZLIB)
endif()

add_library(MShogiSelfPlay STATIC
    SelfPlay.cpp
    ../include/SelfPlay.h
)

target_include_directories(MShogiSelfPlay PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(MShogiSelfPlay PUBLIC MShogiCore MShogiZlib)

add_executable(mshogi_selfplay SelfPlayMain.cpp)
target_link_libraries(mshogi_selfplay PRIVATE MShogiSelfPlay)
execute_process(
    COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    OUTPUT_VARIABLE MSHOGI_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT MSHOGI_GIT_COMMIT)
    set(MSHOGI_GIT_COMMIT unknown)
endif()
target_compile_definitions(mshogi_selfplay PRIVATE
    MSHOGI_CORE_COMMIT="${MSHOGI_GIT_COMMIT}"
)

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
