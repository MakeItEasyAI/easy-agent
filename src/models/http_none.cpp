// Used when libcurl is not found at build time.
#include "models/http.h"

namespace easy::models {

bool httpAvailable() { return false; }

HttpResponse httpPostJson(const std::string&, const std::vector<std::string>&,
                          const std::string&) {
    return {false, 0, "", "this build has no HTTPS support (libcurl not found)"};
}

}  // namespace easy::models
