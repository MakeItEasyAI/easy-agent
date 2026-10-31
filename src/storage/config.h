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
    bool not_found = false;   // true when the file simply does not exist yet
};

std::string defaultConfigPath();                    // ~/.makeiteasy/config.json, expanded
ConfigLoadResult parseConfig(const std::string& jsonText);
ConfigLoadResult loadConfigFile(const std::string& path);

// Writes the config (creating the folder if needed). Returns "" or an error.
std::string saveConfigFile(const std::string& path, const Config& config);

// Folders that are too broad to allow ("/", "~" itself). Returns a reason, or "".
std::string tooBroadReason(const std::string& folder);

}  // namespace easy::storage
