cmake_minimum_required(VERSION 3.14)

project(MShogi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 COMPONENTS Core Widgets Gui Multimedia REQUIRED)

include(FetchContent)
FetchContent_Declare(
  googletest
  # 使用 file:// 协议 + CMAKE_CURRENT_SOURCE_DIR 变量
  URL file://${CMAKE_CURRENT_SOURCE_DIR}/tests/googletest-1.14.0.zip
)
FetchContent_MakeAvailable(googletest)

add_subdirectory(src)
enable_testing()
add_subdirectory(tests)
