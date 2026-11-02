#include "models/model_factory.h"

#include <cstdlib>
#include <vector>

#include "models/fallback_provider.h"
#include "models/gemini_provider.h"
#include "models/ollama_provider.h"

namespace easy::models {

namespace {

std::string env(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? "" : value;
}

std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t");
    if (first == std::string::npos) return "";
    return s.substr(first, s.find_last_not_of(" \t") - first + 1);
}

// One model, e.g. "ollama:qwen2.5:3b" or "gemini".
std::unique_ptr<ModelProvider> makeOne(const std::string& spec, std::string& error) {
    const auto colon = spec.find(':');
    const std::string provider = spec.substr(0, colon);
    const std::string model = colon == std::string::npos ? "" : spec.substr(colon + 1);

    if (provider == "ollama") {
        const std::string host = env("OLLAMA_HOST").empty() ? OllamaProvider::kDefaultHost
                                                            : env("OLLAMA_HOST");
        return std::make_unique<OllamaProvider>(model.empty() ? OllamaProvider::kDefaultModel : model,
                                                host);
    }
    if (provider == "gemini") {
        auto gemini = makeGeminiFromEnv(error);
        if (gemini && !model.empty()) {
            return std::make_unique<GeminiProvider>(geminiApiKey(), model);
        }
        return gemini;
    }
    error = "unknown model \"" + spec + "\" (use ollama[:<model>] or gemini[:<model>])";
    return nullptr;
}

}  // namespace

std::unique_ptr<ModelProvider> makeModel(const std::string& specIn, std::string& error,
                                         std::ostream& notices) {
    std::string spec = trim(specIn.empty() ? env("EASY_MODEL") : specIn);
    if (spec.empty()) spec = "ollama";   // default: local and private

    std::vector<std::string> parts;
    std::string current;
    for (char c : spec + ",") {
        if (c == ',') {
            if (!trim(current).empty()) parts.push_back(trim(current));
            current.clear();
        } else {
            current += c;
        }
    }

    if (parts.size() == 1) return makeOne(parts[0], error);

    std::vector<std::unique_ptr<ModelProvider>> chain;
    for (const std::string& part : parts) {
        std::string partError;
        auto model = makeOne(part, partError);
        if (model) {
            chain.push_back(std::move(model));
        } else if (partError.rfind("unknown model", 0) == 0) {
            error = partError;   // a typo in the list is a real error
            return nullptr;
        } else {
            // e.g. no GEMINI_API_KEY: keep its place, it will say why and fall back
            chain.push_back(std::make_unique<UnavailableProvider>(part, partError));
        }
    }
    return std::make_unique<FallbackProvider>(std::move(chain), notices);
}

}  // namespace easy::models
