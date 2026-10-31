#include <cctype>

#include "platform/ui_automation.h"

namespace easy::platform {

std::string uiNormalize(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        if (c == '*') continue;
        if (std::isspace(c)) {
            if (!out.empty() && out.back() != ' ') out += ' ';
        } else {
            out += static_cast<char>(std::tolower(c));
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

int uiMatchScore(const std::vector<std::string>& labels, const std::string& wanted) {
    const std::string want = uiNormalize(wanted);
    if (want.empty()) return 0;
    int best = 0;
    for (const std::string& raw : labels) {
        const std::string label = uiNormalize(raw);
        if (label.empty()) continue;
        if (label == want) return 2;
        if (label.find(want) != std::string::npos ||
            (label.size() >= 3 && want.find(label) != std::string::npos)) {
            best = 1;
        }
    }
    return best;
}

}  // namespace easy::platform
