#include "models/gemini_provider.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

#include <nlohmann/json.hpp>

#include "actions/path_utils.h"
#include "models/http.h"

namespace easy::models {

using nlohmann::json;

GeminiProvider::GeminiProvider(std::string apiKey, std::string model)
    : apiKey_(std::move(apiKey)), model_(std::move(model)) {}

std::string buildGeminiRequest(const std::string& prompt, bool jsonOutput) {
    json body = {
        {"contents", json::array({{{"parts", json::array({{{"text", prompt}}})}}})},
        {"generationConfig", {{"temperature", 0}}},   // same task -> same plan
    };
    if (jsonOutput) body["generationConfig"]["responseMimeType"] = "application/json";
    return body.dump();   // json escapes quotes/newlines in the prompt safely
}

ModelReply parseGeminiResponse(long httpStatus, const std::string& body) {
    const json j = json::parse(body, nullptr, /*allow_exceptions=*/false);

    if (httpStatus != 200) {
        std::string message = "HTTP " + std::to_string(httpStatus);
        if (!j.is_discarded() && j.contains("error") && j["error"].is_object() &&
            j["error"].contains("message") && j["error"]["message"].is_string()) {
            message += ": " + j["error"]["message"].get<std::string>();
        }
        if (httpStatus == 404) {
            message += "\nTip: choose another model with  export GEMINI_MODEL=<model name>";
        }
        return {false, "", "Gemini error " + message};
    }
    if (j.is_discarded() || !j.is_object()) return {false, "", "Gemini sent invalid JSON"};

    if (!j.contains("candidates") || !j["candidates"].is_array() || j["candidates"].empty()) {
        std::string why = "no answer";
        if (j.contains("promptFeedback") && j["promptFeedback"].contains("blockReason")) {
            why = "blocked: " + j["promptFeedback"]["blockReason"].dump();
        }
        return {false, "", "Gemini gave " + why};
    }

    const json& candidate = j["candidates"][0];
    if (!candidate.contains("content") || !candidate["content"].contains("parts") ||
        !candidate["content"]["parts"].is_array()) {
        return {false, "", "Gemini answer has no text"};
    }

    std::string text;
    for (const json& part : candidate["content"]["parts"]) {
        if (part.contains("text") && part["text"].is_string()) text += part["text"].get<std::string>();
    }
    if (text.empty()) return {false, "", "Gemini answer has no text"};
    return {true, text, ""};
}

ModelReply GeminiProvider::ask(const std::string& prompt) { return send(prompt, true); }

ModelReply GeminiProvider::askText(const std::string& prompt) { return send(prompt, false); }

ModelReply GeminiProvider::send(const std::string& prompt, bool jsonOutput) {
    const std::string url = "https://generativelanguage.googleapis.com/v1beta/models/" +
                            model_ + ":generateContent";
    HttpOptions options;
    options.timeout_seconds = 45;   // if the network stalls the call, fail over to local sooner
    const HttpResponse http = httpPostJson(
        url, {"Content-Type: application/json", "x-goog-api-key: " + apiKey_},
        buildGeminiRequest(prompt, jsonOutput), options);

    if (!http.ok) return {false, "", "could not reach Gemini: " + http.error};
    return parseGeminiResponse(http.status, http.body);
}

std::unique_ptr<ModelProvider> makeGeminiFromEnv(std::string& error) {
    if (!httpAvailable()) {
        error = "this build has no HTTPS support (libcurl was not found when building)";
        return nullptr;
    }
    const std::string key = geminiApiKey();
    if (key.empty()) {
        error = "no Gemini API key (set GEMINI_API_KEY, or run \"easy model\" and pick Gemini)";
        return nullptr;
    }
    const char* model = std::getenv("GEMINI_MODEL");
    const std::string modelName =
        (model != nullptr && *model != '\0') ? model : GeminiProvider::kDefaultModel;
    return std::make_unique<GeminiProvider>(key, modelName);
}

std::string geminiKeyFilePath() { return actions::expandHome("~/.makeiteasy/gemini_api_key"); }

std::string geminiApiKey() {
    const char* env = std::getenv("GEMINI_API_KEY");
    if (env != nullptr && *env != '\0') return env;   // the env var always wins
    std::ifstream file(geminiKeyFilePath());
    if (!file) return "";
    std::string key;
    std::getline(file, key);
    const auto first = key.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    return key.substr(first, key.find_last_not_of(" \t\r\n") - first + 1);
}

std::string saveGeminiApiKey(const std::string& key) {
    namespace fs = std::filesystem;
    const std::string path = geminiKeyFilePath();
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    if (ec) return "cannot create folder for " + path + ": " + ec.message();
    std::ofstream out(path, std::ios::trunc);
    if (!out) return "cannot write " + path;
    out << key << "\n";
    out.close();
#if !defined(_WIN32)
    ::chmod(path.c_str(), 0600);   // only this user can read the key
#endif
    return "";
}

}  // namespace easy::models
