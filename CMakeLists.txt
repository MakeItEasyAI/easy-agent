cmake_minimum_required(VERSION 3.20)
project(easy_agent VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Core library (everything except main) so tests can link it.
add_library(easy_core
    src/cli/cli.cpp
)
target_include_directories(easy_core PUBLIC src)
target_compile_definitions(easy_core PUBLIC EASY_VERSION="${PROJECT_VERSION}")

# Platform-specific sources are added here as they are implemented.
# if(APPLE)
#   target_sources(easy_core PRIVATE src/platform/mac/mac_platform.mm)
# elseif(WIN32)
#   target_sources(easy_core PRIVATE src/platform/windows/windows_platform.cpp)
# else()
#   target_sources(easy_core PRIVATE src/platform/linux/linux_platform.cpp)
# endif()

# The command users run: `easy`
add_executable(easy src/main.cpp)
target_link_libraries(easy PRIVATE easy_core)

# Tests
enable_testing()
add_executable(easy_tests tests/test_main.cpp)
target_link_libraries(easy_tests PRIVATE easy_core)
add_test(NAME easy_tests COMMAND easy_tests)
