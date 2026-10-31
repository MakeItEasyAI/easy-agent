#include "actions/path_utils.h"

#include <cctype>
#include <cstdlib>

namespace easy::actions {

std::string expandHome(const std::string& path) {
    if (path.empty() || path[0] != '~') return path;
    if (path.size() > 1 && path[1] != '/') return path;   // "~user" is not supported

    const char* home = std::getenv("HOME");                 // macOS / Linux
    if (home == nullptr) home = std::getenv("USERPROFILE");  // Windows
    if (home == nullptr) return path;

    return std::string(home) + path.substr(1);
}

bool matchesPattern(const std::string& name, const std::string& pattern) {
    auto same = [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) ==
               std::tolower(static_cast<unsigned char>(b));
    };

    std::size_t n = 0, p = 0;
    std::size_t starP = std::string::npos;   // position of the last '*' in pattern
    std::size_t starN = 0;                   // name position when that '*' was seen

    while (n < name.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || same(pattern[p], name[n]))) {
            ++n;
            ++p;                              // characters match: move both
        } else if (p < pattern.size() && pattern[p] == '*') {
            starP = p++;                      // remember '*', first try matching nothing
            starN = n;
        } else if (starP != std::string::npos) {
            p = starP + 1;                    // mismatch: let the last '*' eat one more char
            n = ++starN;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;   // trailing '*' match nothing
    return p == pattern.size();
}

}  // namespace easy::actions
