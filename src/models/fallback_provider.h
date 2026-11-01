#pragma once

#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

#include "models/model_provider.h"

// Tries models in order; if one cannot answer (no internet, timeout, bad key,
// model gone...), tells the user and tries the next one.
//   e.g. gemini -> ollama:qwen2.5:3b
namespace easy::models {

class FallbackProvider : public ModelProvider {
public:
    FallbackProvider(std::vector<std::unique_ptr<ModelProvider>> providers, std::ostream& notices);

    std::string name() const override;   // "gemini:x -> ollama:y"
    bool isLocal() const override;       // true only if every model is local
    ModelReply ask(const std::string& prompt) override;
    ModelReply askText(const std::string& prompt) override;

private:
    ModelReply tryAll(const std::string& prompt, bool text);

    std::vector<std::unique_ptr<ModelProvider>> providers_;
    std::ostream& notices_;
    std::size_t start_ = 0;   // index of the model that last worked
};

// Placeholder for a model that could not be set up (e.g. no API key).
// Always fails with that reason, so the fallback moves on and says why.
class UnavailableProvider : public ModelProvider {
public:
    UnavailableProvider(std::string name, std::string reason)
        : name_(std::move(name)), reason_(std::move(reason)) {}
    std::string name() const override { return name_; }
    ModelReply ask(const std::string&) override { return {false, "", reason_}; }

private:
    std::string name_;
    std::string reason_;
};

}  // namespace easy::models
