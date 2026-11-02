#pragma once

#include <memory>
#include <string>

#include "models/model_provider.h"

// Google Gemini over HTTPS.
//   Key:   environment variable GEMINI_API_KEY, else a user-only (0600) file
//          ~/.makeiteasy/gemini_api_key written by `easy model`. The env var
//          always wins. The key is never stored in config.json and never in git.
//   Model: environment variable GEMINI_MODEL      (default below)
// The key is sent in a request header, never in the URL, and never printed.
namespace easy::models {

class GeminiProvider : public ModelProvider {
public:
    // Default from Google's own error reply when gemini-2.5-flash was retired for
    // new users. Override any time with:  export GEMINI_MODEL=<model name>
    static constexpr const char* kDefaultModel = "gemini-3.8-flash";

    GeminiProvider(std::string apiKey, std::string model);

    std::string name() const override { return "gemini:" + model_; }
    ModelReply ask(const std::string& prompt) override;
    ModelReply askText(const std::string& prompt) override;

private:
    ModelReply send(const std::string& prompt, bool jsonOutput);

    std::string apiKey_;
    std::string model_;
};

// Reads GEMINI_API_KEY / GEMINI_MODEL. Returns nullptr and sets `error` if
// the key is missing or this build has no HTTPS support.
std::unique_ptr<ModelProvider> makeGeminiFromEnv(std::string& error);

// API key handling. The environment variable GEMINI_API_KEY always takes
// priority; otherwise the key is read from / written to a user-only (0600)
// file in ~/.makeiteasy. The key is never put in config.json or git.
std::string geminiKeyFilePath();                       // ~/.makeiteasy/gemini_api_key
std::string geminiApiKey();                            // env, else the saved file, else ""
std::string saveGeminiApiKey(const std::string& key);  // write the file (0600); "" or an error

// Pure helpers (no network) so they can be unit-tested.
std::string buildGeminiRequest(const std::string& prompt, bool jsonOutput = true);
ModelReply parseGeminiResponse(long httpStatus, const std::string& body);

}  // namespace easy::models
