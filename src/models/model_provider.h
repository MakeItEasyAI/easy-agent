#pragma once

#include <string>

// Common interface for every AI brain. Implementations (planned):
//   GeminiProvider, ClaudeProvider, LocalProvider (llama.cpp)
// API keys come from environment variables, never from files in the repo.
namespace easy::models {

class ModelProvider {
public:
    virtual ~ModelProvider() = default;
    virtual std::string name() const = 0;
    virtual std::string ask(const std::string& prompt) = 0;
};

}  // namespace easy::models
