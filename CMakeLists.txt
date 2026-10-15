cmake_minimum_required(VERSION 3.20)
project(FencingRobotStarter VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FENCING_BUILD_UI "Build Qt 6 Widgets on-board UI" ON)
option(FENCING_BUILD_TESTS "Build core tests" ON)

add_library(fencing_core STATIC
    src/core/application/FencingRobotApplication.cpp
    src/core/application/MotionOrchestrator.cpp
)
target_include_directories(fencing_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(fencing_core PUBLIC cxx_std_20)

add_library(fencing_adapters STATIC
    src/adapters/motion/SimulatedMotionAdapter.cpp
    src/adapters/persistence/InMemoryRobotDataAdapter.cpp
)
target_include_directories(fencing_adapters PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(fencing_adapters PUBLIC fencing_core)

if(FENCING_BUILD_UI)
    find_package(Qt6 COMPONENTS Widgets QUIET)
    if(Qt6_FOUND)
        add_executable(fencing_robot_ui
            src/main/main.cpp
            src/ui/MainWindow.cpp
        )
        target_link_libraries(fencing_robot_ui PRIVATE fencing_core fencing_adapters Qt6::Widgets)
        message(STATUS "Qt 6 Widgets found: building fencing_robot_ui")
    else()
        message(WARNING "Qt 6 Widgets not found: UI target skipped; core tests can still build. Install qt6-base-dev.")
    endif()
endif()

if(FENCING_BUILD_TESTS)
    include(CTest)
    add_executable(fencing_core_tests tests/CoreTests.cpp)
    target_link_libraries(fencing_core_tests PRIVATE fencing_core fencing_adapters)
    add_test(NAME core_tests COMMAND fencing_core_tests)
endif()
