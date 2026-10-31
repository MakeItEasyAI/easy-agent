#pragma once

#include <string>

// Common interface for every AI brain. Implementations (planned):
//   GeminiProvider, ClaudeProvider, LocalProvider (llama.cpp)
// API keys come from environment variables, never from files in the repo.
namespace easy::models {

struct ModelReply {
    bool ok = false;
    std::string text;    // the model's answer when ok
    std::string error;   // why it failed (network, bad key, ...) when not ok
};

class ModelProvider {
public:
    virtual ~ModelProvider() = default;
    virtual std::string name() const = 0;
    virtual ModelReply ask(const std::string& prompt) = 0;
};

}  // namespace easy::models
