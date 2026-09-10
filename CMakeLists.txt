cmake_minimum_required(VERSION 3.16)

project(MShogi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(MSHOGI_BUILD_APP "Build the Qt desktop application" ON)

if(MSHOGI_BUILD_APP)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)
    find_package(Qt6 6.8 COMPONENTS Core Widgets Gui Multimedia Concurrent REQUIRED)
endif()

add_subdirectory(src)

include(CTest)
# 发布构建可关闭 BUILD_TESTING，避免引入 Qt Test 和 Google Test。
if(BUILD_TESTING)
    if(NOT MSHOGI_BUILD_APP)
        message(FATAL_ERROR "BUILD_TESTING requires MSHOGI_BUILD_APP=ON")
    endif()
    find_package(Qt6 6.8 COMPONENTS Test REQUIRED)

    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL file://${CMAKE_CURRENT_SOURCE_DIR}/tests/googletest-1.14.0.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(googletest)
    add_subdirectory(tests)
endif()
