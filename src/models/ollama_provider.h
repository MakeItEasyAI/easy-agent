#pragma once

#include <string>

#include "models/model_provider.h"

// A local model served by Ollama on this computer (http://localhost:11434).
// Nothing is sent to the internet. Ollama itself runs llama.cpp.
//   Host:  environment variable OLLAMA_HOST (default http://localhost:11434,
//          must be localhost / 127.0.0.1)
namespace easy::models {

class OllamaProvider : public ModelProvider {
public:
    static constexpr const char* kDefaultModel = "qwen2.5:3b";
    static constexpr const char* kDefaultHost = "http://localhost:11434";

    OllamaProvider(std::string model, std::string host = kDefaultHost);

    std::string name() const override { return "ollama:" + model_; }
    bool isLocal() const override { return true; }
    ModelReply ask(const std::string& prompt) override;
    ModelReply askText(const std::string& prompt) override;

private:
    ModelReply send(const std::string& prompt, bool jsonOutput);

    std::string model_;
    std::string host_;
};

// Pure helpers (no network) so they can be unit-tested.
std::string buildOllamaRequest(const std::string& model, const std::string& prompt,
                               bool jsonOutput = true);
ModelReply parseOllamaResponse(long httpStatus, const std::string& body, const std::string& model);

}  // namespace easy::models
