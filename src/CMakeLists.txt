set(MSHOGI_LIB_SOURCES
    GameEngine.cpp
    UIController.cpp
)

set(MSHOGI_LIB_HEADERS
    ../include/GameEngine.h
    ../include/UIController.h
)

add_library(MShogiLib STATIC
    ${MSHOGI_LIB_SOURCES}
    ${MSHOGI_LIB_HEADERS}
)

target_include_directories(MShogiLib PUBLIC ../include)

target_link_libraries(MShogiLib PUBLIC
    Qt5::Core
    Qt5::Widgets
    Qt5::Gui
)

add_executable(MShogiApp
    main.cpp
)

target_link_libraries(MShogiApp PRIVATE
    MShogiLib
    Qt5::Core
    Qt5::Widgets
    Qt5::Gui
)