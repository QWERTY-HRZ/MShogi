find_package(Qt6 COMPONENTS Core Widgets Gui REQUIRED)

add_executable(MShogiTests
    tst_ruleengine.cpp
#    tst_board.cpp
#    tst_game.cpp
)
# 将 GTest::gtest_main 改为 gtest_main
target_link_libraries(MShogiTests PRIVATE
    MShogiLib
    gtest
    gtest_main
    Qt6::Core
)
include(GoogleTest)
gtest_discover_tests(MShogiTests)

add_executable(UITests
    tst_ui_gtest.cpp  # 文件名改一下以示区别
)
# 链接 Qt Test 和 UI 库，只链接核心库
target_link_libraries(UITests PRIVATE
    MShogiLib
    gtest
    Qt6::Core
    Qt6::Widgets
    Qt6::Gui
)
# 注册为普通测试
gtest_discover_tests(UITests)
