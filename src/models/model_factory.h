#pragma once

#include <memory>
#include <string>

#include "models/model_provider.h"

// Picks the AI model from a spec string:
//   "ollama:qwen2.5:3b"     local model via Ollama (nothing leaves the computer)
//   "ollama"                local, default model
//   "gemini:<model>"        Google Gemini (needs GEMINI_API_KEY)
//   "gemini"                Gemini, default model
//   ""                      use EASY_MODEL env var; if that is empty too:
//                           local Ollama (private by default)
namespace easy::models {

std::unique_ptr<ModelProvider> makeModel(const std::string& spec, std::string& error);

}  // namespace easy::models
