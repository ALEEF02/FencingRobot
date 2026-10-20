cmake_minimum_required(VERSION 3.20)
project(FencingRobotStarter VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/bridges")
configure_file(bridges/lerobot_bridge.py
               "${CMAKE_BINARY_DIR}/bridges/lerobot_bridge.py" COPYONLY)
configure_file(bridges/lerobot_bridge.example.json
               "${CMAKE_BINARY_DIR}/bridges/lerobot_bridge.example.json" COPYONLY)

option(FENCING_BUILD_UI "Build Qt 6 Widgets on-board UI" ON)
option(FENCING_BUILD_TESTS "Build core tests" ON)

find_package(SQLite3 REQUIRED)

add_library(fencing_core STATIC
    src/core/application/FencingRobotApplication.cpp
    src/core/application/MotionOrchestrator.cpp
)
target_include_directories(fencing_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(fencing_core PUBLIC cxx_std_20)

add_library(fencing_adapters STATIC
    src/adapters/motion/SimulatedMotionAdapter.cpp
    src/adapters/persistence/InMemoryRobotDataAdapter.cpp
    src/adapters/persistence/SqliteRobotDataAdapter.cpp
)
target_include_directories(fencing_adapters PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(fencing_adapters PUBLIC fencing_core SQLite::SQLite3)

find_package(Qt6 COMPONENTS Core QUIET)
if(Qt6Core_FOUND)
    add_library(fencing_lerobot_adapter STATIC
        src/adapters/motion/LeRobotMotionAdapter.cpp
    )
    target_include_directories(fencing_lerobot_adapter PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(fencing_lerobot_adapter PUBLIC fencing_core Qt6::Core)
endif()

if(FENCING_BUILD_UI)
    find_package(Qt6 COMPONENTS Widgets QUIET)
    if(Qt6_FOUND)
        add_executable(fencing_robot_ui
            src/main/main.cpp
            src/ui/MainWindow.cpp
        )
        target_link_libraries(fencing_robot_ui PRIVATE
            fencing_core fencing_adapters fencing_lerobot_adapter Qt6::Widgets)
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

    add_executable(fencing_adapter_tests tests/AdapterTests.cpp)
    target_link_libraries(fencing_adapter_tests PRIVATE fencing_core fencing_adapters SQLite::SQLite3)
    add_test(NAME adapter_tests COMMAND fencing_adapter_tests)

    if(Qt6Core_FOUND)
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_Interpreter_FOUND)
            add_executable(fencing_motion_adapter_tests tests/MotionAdapterTests.cpp)
            target_link_libraries(fencing_motion_adapter_tests PRIVATE fencing_lerobot_adapter Qt6::Core)
            target_compile_definitions(fencing_motion_adapter_tests PRIVATE
                FENCING_TEST_PYTHON="${Python3_EXECUTABLE}"
                FENCING_FAKE_BRIDGE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fake_lerobot_bridge.py"
            )
            add_test(NAME motion_adapter_tests COMMAND fencing_motion_adapter_tests)
            add_test(NAME python_bridge_tests
                COMMAND "${Python3_EXECUTABLE}" -m unittest tests/test_lerobot_bridge.py -v)
            set_tests_properties(python_bridge_tests PROPERTIES
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        endif()
    endif()
endif()
