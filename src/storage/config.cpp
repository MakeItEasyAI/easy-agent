#include "storage/config.h"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "actions/path_utils.h"

namespace easy::storage {

namespace {

ConfigLoadResult fail(const std::string& message) {
    return {std::nullopt, message};
}

}  // namespace

std::string defaultConfigPath() {
    return actions::expandHome("~/.makeiteasy/config.json");
}

ConfigLoadResult parseConfig(const std::string& jsonText) {
    const auto j = nlohmann::json::parse(jsonText, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) return fail("invalid JSON");
    if (!j.is_object()) return fail("config must be a JSON object");

    if (!j.contains("allowed_folders") || !j["allowed_folders"].is_array() ||
        j["allowed_folders"].empty())
        return fail("\"allowed_folders\" must be a non-empty list of folders");

    Config config;
    for (const auto& folder : j["allowed_folders"]) {
        if (!folder.is_string() || folder.get<std::string>().empty())
            return fail("every entry in \"allowed_folders\" must be a folder path");
        config.allowed_folders.push_back(folder.get<std::string>());
    }
    return {std::move(config), ""};
}

ConfigLoadResult loadConfigFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) return fail("config not found: " + path);

    std::stringstream buffer;
    buffer << file.rdbuf();
    ConfigLoadResult result = parseConfig(buffer.str());
    if (!result.config) result.error = path + ": " + result.error;
    return result;
}

}  // namespace easy::storage
