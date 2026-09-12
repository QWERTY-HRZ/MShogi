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

if(MSHOGI_ENABLE_ONNX_AGENT)
    file(TO_CMAKE_PATH "${MSHOGI_ONNXRUNTIME_ROOT}/onnxruntime.dll"
         MSHOGI_TEST_ONNX_RUNTIME_PATH)
    file(TO_CMAKE_PATH "${MSHOGI_ONNX_MODEL}" MSHOGI_TEST_ONNX_MODEL_PATH)
    target_compile_definitions(MShogiTests PRIVATE
        MSHOGI_TEST_ONNX_RUNTIME="${MSHOGI_TEST_ONNX_RUNTIME_PATH}"
        MSHOGI_TEST_ONNX_MODEL="${MSHOGI_TEST_ONNX_MODEL_PATH}"
    )
endif()

include(GoogleTest)
gtest_discover_tests(MShogiTests)
