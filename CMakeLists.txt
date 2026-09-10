cmake_minimum_required(VERSION 3.16)

project(MShogi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.8 COMPONENTS Core Widgets Gui Multimedia REQUIRED)

add_subdirectory(src)

include(CTest)
# 发布构建可关闭 BUILD_TESTING，避免引入 Qt Test 和 Google Test。
if(BUILD_TESTING)
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
