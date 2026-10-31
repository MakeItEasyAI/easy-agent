#include "core/safety_guard.h"

#include <algorithm>
#include <sstream>
#include <system_error>

#include "actions/path_utils.h"

namespace easy::core {

namespace fs = std::filesystem;

namespace {

// Turn any path into its real, absolute form:
//   "~/Downloads/../x" -> "/Users/you/x", symlinks followed.
// Returns an empty path if it cannot be resolved.
fs::path resolve(const std::string& raw) {
    std::error_code ec;
    fs::path p = fs::absolute(actions::expandHome(raw), ec);
    if (ec) return {};
    p = fs::weakly_canonical(p, ec);   // works even if the file does not exist yet
    if (ec) return {};
    p = p.lexically_normal();
    if (!p.has_filename() && p.has_parent_path() && p != p.root_path()) {
        p = p.parent_path();           // "/a/b/" -> "/a/b"
    }
    return p;
}

// Compare whole path parts, so "/a/Downloads_evil" is NOT inside "/a/Downloads".
bool isInside(const fs::path& child, const fs::path& parent) {
    const auto [parentIt, childIt] =
        std::mismatch(parent.begin(), parent.end(), child.begin(), child.end());
    return parentIt == parent.end();
}

}  // namespace

FolderSafetyGuard::FolderSafetyGuard(const std::vector<std::string>& allowedFolders) {
    for (const std::string& folder : allowedFolders) {
        fs::path resolved = resolve(folder);
        if (!resolved.empty()) allowed_.push_back(std::move(resolved));
    }
}

bool FolderSafetyGuard::isAllowedPath(const std::string& path) const {
    if (path.empty()) return false;
    const fs::path resolved = resolve(path);
    if (resolved.empty()) return false;
    return std::any_of(allowed_.begin(), allowed_.end(),
                       [&](const fs::path& folder) { return isInside(resolved, folder); });
}

SafetyDecision FolderSafetyGuard::check(RiskLevel risk,
                                        const std::vector<std::string>& paths) const {
    if (risk == RiskLevel::Blocked) {
        return {Decision::Deny, "this action is blocked"};
    }

    for (const std::string& value : paths) {
        std::istringstream lines(value);
        std::string path;
        while (std::getline(lines, path)) {
            if (path.empty()) continue;
            if (!isAllowedPath(path)) {
                return {Decision::Deny, "outside allowed folders: " + path};
            }
        }
    }

    if (risk == RiskLevel::Confirm) return {Decision::AskUser, ""};
    return {Decision::Allow, ""};
}

}  // namespace easy::core
