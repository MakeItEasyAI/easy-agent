cmake_minimum_required(VERSION 3.20)
project(easy_agent VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Dependencies (downloaded at configure time, pinned to an exact version)
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# Core library (everything except main) so tests can link it.
add_library(easy_core
    src/cli/cli.cpp
    src/storage/recipe_json.cpp
    src/storage/config.cpp
    src/storage/recipe_store.cpp
    src/actions/path_utils.cpp
    src/actions/list_files.cpp
    src/actions/move_files.cpp
    src/actions/registry.cpp
    src/actions/open_actions.cpp
    src/actions/browser_actions.cpp
    src/browser/websocket.cpp
    src/browser/cdp.cpp
    src/browser/chrome.cpp
    src/platform/launch.cpp
    src/core/safety_guard.cpp
    src/core/replay_engine.cpp
    src/core/validator.cpp
    src/core/planner.cpp
    src/models/gemini_provider.cpp
    src/models/ollama_provider.cpp
    src/models/model_factory.cpp
    src/models/fallback_provider.cpp
    src/models/http_common.cpp
    src/cli/console_confirmer.cpp
)
target_include_directories(easy_core PUBLIC src)
target_link_libraries(easy_core PUBLIC nlohmann_json::nlohmann_json)
target_compile_definitions(easy_core PUBLIC EASY_VERSION="${PROJECT_VERSION}")

# HTTPS for cloud AI models. Optional: without libcurl the agent still builds,
# and AI commands report that HTTPS is unavailable.
find_package(CURL)
if(CURL_FOUND)
    target_sources(easy_core PRIVATE src/models/http_curl.cpp)
    target_link_libraries(easy_core PUBLIC CURL::libcurl)
else()
    message(STATUS "libcurl not found: AI (Gemini) support disabled in this build")
    target_sources(easy_core PRIVATE src/models/http_none.cpp)
endif()

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
