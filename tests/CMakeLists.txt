add_executable(MShogiTests
    tst_ruleengine.cpp
    tst_board.cpp
    tst_game.cpp
)

# 将 GTest::gtest_main 改为 gtest_main
target_link_libraries(MShogiTests PRIVATE
    MShogiLib
    gtest
    gtest_main
    Qt5::Core
)

include(GoogleTest)
gtest_discover_tests(MShogiTests)
