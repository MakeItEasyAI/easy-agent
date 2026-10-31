#include "storage/config.h"

#include <filesystem>
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
    if (j.contains("model")) {
        if (!j["model"].is_string()) return fail("\"model\" must be text, e.g. \"gemini,ollama\"");
        config.model = j["model"].get<std::string>();
    }
    if (j.contains("allowed_apps")) {
        if (!j["allowed_apps"].is_array()) return fail("\"allowed_apps\" must be a list of app names");
        for (const auto& app : j["allowed_apps"]) {
            if (!app.is_string() || app.get<std::string>().empty())
                return fail("every entry in \"allowed_apps\" must be an app name");
            config.allowed_apps.push_back(app.get<std::string>());
        }
    }
    return {std::move(config), ""};
}

ConfigLoadResult loadConfigFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        ConfigLoadResult missing = fail("config not found: " + path);
        std::error_code ec;
        missing.not_found = !std::filesystem::exists(path, ec);
        return missing;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    ConfigLoadResult result = parseConfig(buffer.str());
    if (!result.config) result.error = path + ": " + result.error;
    return result;
}

std::string saveConfigFile(const std::string& path, const Config& config) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    if (ec) return "cannot create folder for " + path + ": " + ec.message();

    nlohmann::json j = {{"allowed_folders", config.allowed_folders}};
    if (!config.model.empty()) j["model"] = config.model;
    if (!config.allowed_apps.empty()) j["allowed_apps"] = config.allowed_apps;
    std::ofstream out(path, std::ios::trunc);
    if (!out) return "cannot write " + path;
    out << j.dump(2) << "\n";
    return out ? "" : "cannot write " + path;
}

std::string tooBroadReason(const std::string& folder) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path p = fs::weakly_canonical(fs::absolute(actions::expandHome(folder), ec), ec);
    const fs::path home = fs::weakly_canonical(actions::expandHome("~"), ec);
    if (p.empty()) return "cannot understand this path";
    if (p == p.root_path()) return "the whole disk is too broad";
    if (!home.empty() && p == home) return "your whole home folder is too broad";
    return "";
}

}  // namespace easy::storage
