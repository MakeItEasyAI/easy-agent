#pragma once

#include <string>
#include <vector>

// Minimal HTTPS POST used by cloud model providers.
// Implemented with libcurl (http_curl.cpp) when available; otherwise a stub
// (http_none.cpp) that always fails, so the rest of the agent still builds.
namespace easy::models {

struct HttpResponse {
    bool ok = false;      // request completed (any HTTP status)
    long status = 0;      // HTTP status code, e.g. 200
    std::string body;
    std::string error;    // network-level error when !ok
};

bool httpAvailable();

// headers: full lines like "Content-Type: application/json"
HttpResponse httpPostJson(const std::string& url, const std::vector<std::string>& headers,
                          const std::string& body);

}  // namespace easy::models
