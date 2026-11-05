cmake_minimum_required(VERSION 3.28)
project(ChessAI LANGUAGES CXX)

option(CHESS_BUILD_GUI "Build the SFML chess application" ON)
include(CTest)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
    Engines/TacticalEvaluator.cpp
    Engines/TacticalEvaluator.hpp
    transposition_table.cpp
    transposition_table.hpp
    Engines/AggressiveEvaluator.cpp
    Engines/AggressiveEvaluator.hpp
    Engines/PositionalEvaluator.cpp
    Engines/PositionalEvaluator.hpp
    Engines/OldEvaluator.cpp
    Engines/OldEvaluator.hpp
)
target_include_directories(chess_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(chess_core PUBLIC cxx_std_17)
find_package(Threads REQUIRED)
target_link_libraries(chess_core PUBLIC Threads::Threads)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    include(CheckCXXSourceCompiles)
    include(CMakePushCheckState)
    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_FLAGS "-mcx16")
    check_cxx_source_compiles("
        #if !defined(__x86_64__) || !defined(__SIZEOF_INT128__) || !defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16)
        #error Native 128-bit CAS unavailable
        #endif
        int main() {
            alignas(16) unsigned __int128 word = 0;
            return static_cast<int>(__sync_val_compare_and_swap(&word, 0, 1));
        }" CHESS_COMPILER_HAS_NATIVE_CAS128)
    cmake_pop_check_state()
endif()
if(NOT CHESS_COMPILER_HAS_NATIVE_CAS128)
    message(FATAL_ERROR "The CAS-only transposition table requires GCC x86-64 native CMPXCHG16B support")
endif()
set_source_files_properties(transposition_table.cpp PROPERTIES
    COMPILE_OPTIONS "-mcx16" COMPILE_DEFINITIONS CHESS_HAVE_NATIVE_CAS128=1)

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
    add_executable(shared_tt_tests tests/shared_tt_tests.cpp)
    target_link_libraries(shared_tt_tests PRIVATE chess_core)
    add_test(NAME shared_tt_tests COMMAND shared_tt_tests)
    set_tests_properties(shared_tt_tests PROPERTIES TIMEOUT 60)
    add_executable(parallel_search_tests tests/parallel_search_tests.cpp)
    target_link_libraries(parallel_search_tests PRIVATE chess_core)
    add_test(NAME parallel_search_tests COMMAND parallel_search_tests)
    set_tests_properties(parallel_search_tests PROPERTIES TIMEOUT 60)
    add_executable(board_engine_tests tests/board_engine_tests.cpp)
    target_link_libraries(board_engine_tests PRIVATE chess_core)
    add_test(NAME board_engine_tests COMMAND board_engine_tests)
    add_executable(local_store_tests tests/local_store_tests.cpp)
    target_link_libraries(local_store_tests PRIVATE chess_local_store chess_core)
    add_test(NAME local_store_tests COMMAND local_store_tests "${CMAKE_CURRENT_BINARY_DIR}/local-store-test-data")
endif()
