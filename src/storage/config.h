#pragma once

#include <optional>
#include <string>
#include <vector>

// User settings, read from ~/.makeiteasy/config.json:
//   { "allowed_folders": ["~/Downloads", "~/Documents"] }
// No API keys are ever stored here (they come from environment variables).
namespace easy::storage {

struct Config {
    std::vector<std::string> allowed_folders;
};

struct ConfigLoadResult {
    std::optional<Config> config;
    std::string error;
};

std::string defaultConfigPath();                    // ~/.makeiteasy/config.json, expanded
ConfigLoadResult parseConfig(const std::string& jsonText);
ConfigLoadResult loadConfigFile(const std::string& path);

}  // namespace easy::storage
