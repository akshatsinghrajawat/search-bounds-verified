cmake_minimum_required(VERSION 3.16)
project(optimal_search_strategies CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Default to Release if the user doesn't specify a build type, so a plain
# `cmake -B build && cmake --build build` doesn't silently benchmark an
# unoptimized 100k-trial Monte Carlo run.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()
add_compile_options(-Wall -Wextra -Wpedantic)
add_executable(guess number_guessing_game.cpp)
add_executable(tests tests.cpp)
add_executable(knuth_bst knuth_optimal_bst.cpp)
add_executable(noisy_search noisy_oracle_search.cpp)
# Monte Carlo simulation benefits from real optimization.
target_compile_options(guess PRIVATE $<$<CONFIG:Release>:-O3>)
# tests target: match the sanitizer flags the README's g++ command promises,
# scoped only to this target so `guess` doesn't get sanitizer overhead.
target_compile_options(tests PRIVATE $<$<CONFIG:Debug>:-fsanitize=address,undefined -g>)
target_link_options(tests PRIVATE $<$<CONFIG:Debug>:-fsanitize=address,undefined>)
# Release mode defines NDEBUG, which silently compiles out every assert() --
# and assert() is this suite's only test mechanism (no separate framework).
# Without this, `cmake -B build && cmake --build build` (the README's own
# documented command) would build a `tests` binary that runs zero actual
# checks and unconditionally prints "All tests passed." -UNDEBUG placed
# after Release's -DNDEBUG on the command line overrides it for this
# target only; `guess` still gets NDEBUG's other Release behavior.
target_compile_options(tests PRIVATE -UNDEBUG)
