cmake_minimum_required(VERSION 3.16)
project(search_bounds_verified CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Default to Release if the user doesn't specify a build type, so a plain
# `cmake -B build && cmake --build build` doesn't silently benchmark an
# unoptimized 100k-trial Monte Carlo run.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()
add_compile_options(-Wall -Wextra -Wpedantic)

# Windows/MinGW: at runtime, Windows resolves libstdc++-6.dll by searching
# PATH, and it's common to have more than one on PATH -- e.g. one bundled
# with Git for Windows, another from the MSYS2 toolchain that actually
# built these binaries. If an older/mismatched DLL loads instead of the
# one this was compiled against, std::string/std::vector's internal
# layout disagrees across that boundary and the first substantial
# string/vector work segfaults -- confirmed via gdb backtrace pointing
# into libstdc++-6.dll from inside writeSvgLineChart (see the "Nine real
# bugs" list in the README). Static-linking the runtime removes the DLL
# dependency entirely, so there's nothing left for PATH order to get
# wrong. No effect on non-Windows builds.
if(MINGW)
    add_link_options(-static-libgcc -static-libstdc++)
endif()

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
