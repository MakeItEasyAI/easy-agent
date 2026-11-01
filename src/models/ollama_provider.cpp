#include "models/ollama_provider.h"

#include <nlohmann/json.hpp>

#include "models/http.h"

namespace easy::models {

using nlohmann::json;

OllamaProvider::OllamaProvider(std::string model, std::string host)
    : model_(std::move(model)), host_(std::move(host)) {
    while (!host_.empty() && host_.back() == '/') host_.pop_back();
}

std::string buildOllamaRequest(const std::string& model, const std::string& prompt, bool jsonOutput) {
    json body = {
        {"model", model},
        {"prompt", prompt},
        {"stream", false},   // one complete answer
        // temperature 0: same task -> same plan. num_ctx: room for long prompts
        // (a document + instructions); Ollama's small default would silently
        // cut the start of the prompt.
        {"options", {{"temperature", 0}, {"num_ctx", 8192}}},
    };
    if (jsonOutput) body["format"] = "json";   // force valid JSON output
    return body.dump();
}

ModelReply parseOllamaResponse(long httpStatus, const std::string& body, const std::string& model) {
    const json j = json::parse(body, nullptr, /*allow_exceptions=*/false);
    std::string serverError;
    if (!j.is_discarded() && j.is_object() && j.contains("error") && j["error"].is_string()) {
        serverError = j["error"].get<std::string>();
    }

    if (httpStatus == 404) {
        return {false, "",
                "Ollama model \"" + model + "\" is not downloaded" +
                    (serverError.empty() ? "" : " (" + serverError + ")") +
                    "\nTip: run  ollama pull " + model + "   or choose another with --model"};
    }
    if (httpStatus != 200) {
        return {false, "",
                "Ollama error HTTP " + std::to_string(httpStatus) +
                    (serverError.empty() ? "" : ": " + serverError)};
    }
    if (j.is_discarded() || !j.is_object()) return {false, "", "Ollama sent invalid JSON"};
    if (!serverError.empty()) return {false, "", "Ollama error: " + serverError};
    if (!j.contains("response") || !j["response"].is_string() ||
        j["response"].get<std::string>().empty()) {
        return {false, "", "Ollama answer has no text"};
    }
    return {true, j["response"].get<std::string>(), ""};
}

ModelReply OllamaProvider::ask(const std::string& prompt) { return send(prompt, true); }

ModelReply OllamaProvider::askText(const std::string& prompt) { return send(prompt, false); }

ModelReply OllamaProvider::send(const std::string& prompt, bool jsonOutput) {
    if (!isLocalHttpUrl(host_ + "/")) {
        return {false, "", "OLLAMA_HOST must be http://localhost:<port> or http://127.0.0.1:<port>"};
    }
    HttpOptions options;
    options.allow_local_http = true;
    options.timeout_seconds = 300;   // first call loads the model into memory, can be slow

    const HttpResponse http = httpPostJson(host_ + "/api/generate",
                                           {"Content-Type: application/json"},
                                           buildOllamaRequest(model_, prompt, jsonOutput), options);
    if (!http.ok) {
        return {false, "",
                "could not reach Ollama at " + host_ + ": " + http.error +
                    "\nTip: start it with  ollama serve   (or open the Ollama app)"};
    }
    return parseOllamaResponse(http.status, http.body, model_);
}

}  // namespace easy::models
