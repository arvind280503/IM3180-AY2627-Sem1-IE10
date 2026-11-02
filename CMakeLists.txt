cmake_minimum_required(VERSION 3.28)
project(ChessAI LANGUAGES CXX)

option(CHESS_BUILD_GUI "Build the SFML chess application" ON)
include(CTest)

set(CMAKE_CXX_EXTENSIONS OFF)

add_library(chess_core STATIC
    board.cpp
    board.hpp
    evaluator.cpp
    evaluator.hpp
    engine.cpp
    engine.hpp
    Engines/NewEvaluator.cpp
    Engines/NewEvaluator.hpp
    Engines/SecondEvaluator.cpp
    Engines/SecondEvaluator.hpp
)
target_include_directories(chess_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(chess_core PUBLIC cxx_std_17)

find_package(SQLite3 REQUIRED)
add_library(chess_local_store STATIC storage/local_store.cpp storage/local_store.hpp)
target_include_directories(chess_local_store PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(chess_local_store PUBLIC cxx_std_17)
target_link_libraries(chess_local_store PUBLIC SQLite::SQLite3)
if(WIN32)
    target_link_libraries(chess_local_store PRIVATE bcrypt)
else()
    find_package(OpenSSL REQUIRED COMPONENTS Crypto)
    target_link_libraries(chess_local_store PRIVATE OpenSSL::Crypto)
endif()

if(CHESS_BUILD_GUI)
    find_package(Threads REQUIRED)
    include(FetchContent)

    # The UI uses both the Graphics and Audio modules.
    set(SFML_BUILD_AUDIO ON CACHE BOOL "Build SFML's Audio module" FORCE)
    set(SFML_BUILD_NETWORK OFF CACHE BOOL "Build SFML's Network module" FORCE)

    FetchContent_Declare(SFML
        GIT_REPOSITORY https://github.com/SFML/SFML.git
        GIT_TAG 3.0.2
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL
        SYSTEM
    )
    FetchContent_MakeAvailable(SFML)

    add_executable(chess main.cpp "UI files/ui.cpp" "UI files/ui.hpp")
    set_target_properties(chess PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    target_link_libraries(chess PRIVATE chess_core chess_local_store SFML::Graphics SFML::Audio Threads::Threads)
    target_compile_definitions(chess PRIVATE CHESS_DATA_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/data")
    if(BUILD_TESTING)
        add_executable(ui_account_history_tests tests/ui_account_history_tests.cpp "UI files/ui.cpp")
        target_link_libraries(ui_account_history_tests PRIVATE chess_core chess_local_store SFML::Graphics SFML::Audio Threads::Threads)
        add_test(NAME ui_account_history_tests COMMAND ui_account_history_tests "${CMAKE_CURRENT_BINARY_DIR}/ui-account-test-data")
        set_tests_properties(ui_account_history_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
        add_executable(ui_search_worker_tests tests/ui_search_worker_tests.cpp "UI files/ui.cpp")
        target_link_libraries(ui_search_worker_tests PRIVATE chess_core SFML::Graphics SFML::Audio Threads::Threads)
        add_test(NAME ui_search_worker_tests COMMAND ui_search_worker_tests)
        set_tests_properties(ui_search_worker_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" TIMEOUT 60)
    endif()
endif()

if(BUILD_TESTING)
    add_executable(board_engine_tests tests/board_engine_tests.cpp)
    target_link_libraries(board_engine_tests PRIVATE chess_core)
    add_test(NAME board_engine_tests COMMAND board_engine_tests)
    add_executable(local_store_tests tests/local_store_tests.cpp)
    target_link_libraries(local_store_tests PRIVATE chess_local_store chess_core)
    add_test(NAME local_store_tests COMMAND local_store_tests "${CMAKE_CURRENT_BINARY_DIR}/local-store-test-data")
endif()
