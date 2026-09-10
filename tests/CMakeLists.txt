add_executable(MShogiTests
    test_main.cpp
    tst_board.cpp
    tst_ruleengine.cpp
    tst_game.cpp
    tst_ui_gtest.cpp
    tst_openingdraw.cpp
    tst_setupdialog.cpp
    tst_gamecore.cpp
    tst_gameagent.cpp
    tst_selfplay.cpp
)

target_link_libraries(MShogiTests PRIVATE
    MShogiLib
    MShogiSelfPlay
    gtest
    Qt6::Core
    Qt6::Widgets
    Qt6::Gui
    Qt6::Test
)

include(GoogleTest)
gtest_discover_tests(MShogiTests)
