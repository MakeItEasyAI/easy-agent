#include "models/model_factory.h"

#include <cstdlib>

#include "models/gemini_provider.h"
#include "models/ollama_provider.h"

namespace easy::models {

namespace {

std::string env(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? "" : value;
}

}  // namespace

std::unique_ptr<ModelProvider> makeModel(const std::string& specIn, std::string& error) {
    std::string spec = specIn.empty() ? env("EASY_MODEL") : specIn;
    if (spec.empty()) spec = "ollama";   // default: local and private

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
            return std::make_unique<GeminiProvider>(env("GEMINI_API_KEY"), model);
        }
        return gemini;
    }

    error = "unknown model \"" + spec + "\" (use ollama:<model> or gemini:<model>)";
    return nullptr;
}

}  // namespace easy::models
