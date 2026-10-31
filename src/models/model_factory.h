#pragma once

#include <iosfwd>
#include <memory>
#include <string>

#include "models/model_provider.h"

// Picks the AI model(s) from a spec string:
//   "ollama:qwen2.5:3b"     local model via Ollama (nothing leaves the computer)
//   "ollama"                local, default model
//   "gemini:<model>"        Google Gemini (needs GEMINI_API_KEY)
//   "gemini"                Gemini, default model
//   "gemini,ollama"         try in order: if Gemini cannot answer, use local
//   ""                      use EASY_MODEL env var; if empty too: local Ollama
// `notices` receives "X failed, trying Y" messages when falling back.
namespace easy::models {

std::unique_ptr<ModelProvider> makeModel(const std::string& spec, std::string& error,
                                         std::ostream& notices);

}  // namespace easy::models
