#pragma once

#include <string>
#include <vector>

// Minimal HTTP POST used by model providers.
// Implemented with libcurl (http_curl.cpp) when available; otherwise a stub
// (http_none.cpp) that always fails, so the rest of the agent still builds.
namespace easy::models {

struct HttpResponse {
    bool ok = false;      // request completed (any HTTP status)
    long status = 0;      // HTTP status code, e.g. 200
    std::string body;
    std::string error;    // network-level error when !ok
};

struct HttpOptions {
    long timeout_seconds = 90;
    // Plain http:// is refused unless this is true AND the host is
    // localhost / 127.0.0.1 (used only for local models like Ollama).
    bool allow_local_http = false;
};

bool httpAvailable();

// True for "http://localhost:..." or "http://127.0.0.1:..." only.
bool isLocalHttpUrl(const std::string& url);

// headers: full lines like "Content-Type: application/json"
HttpResponse httpPostJson(const std::string& url, const std::vector<std::string>& headers,
                          const std::string& body, const HttpOptions& options = {});

}  // namespace easy::models
