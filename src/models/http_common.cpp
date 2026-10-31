#include "models/http.h"

namespace easy::models {

bool isLocalHttpUrl(const std::string& url) {
    for (const char* prefix : {"http://localhost:", "http://127.0.0.1:",
                               "http://localhost/", "http://127.0.0.1/"}) {
        if (url.rfind(prefix, 0) == 0) return true;   // url starts with prefix
    }
    return false;
}

}  // namespace easy::models
