#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Every step passes through the safety guard before it runs.
namespace easy::core {

enum class RiskLevel {
    Safe,      // run directly (list files, open app)
    Confirm,   // ask the user (move/delete files, submit forms)
    Blocked    // never run (passwords, sudo)
};

enum class Decision { Allow, AskUser, Deny };

struct SafetyDecision {
    Decision decision = Decision::Deny;
    std::string reason;   // why, shown to the user when denied
};

class SafetyGuard {
public:
    virtual ~SafetyGuard() = default;

    // risk:  the action's risk level
    // paths: every path the action will touch (a value may hold several
    //        paths separated by newlines)
    virtual SafetyDecision check(RiskLevel risk, const std::vector<std::string>& paths) const = 0;
};

// The default guard: risk rules + every path must be inside an allowed folder.
// Paths are fully resolved first ("~", "..", symlinks), so tricks like
// "~/Downloads/../../etc" or a link pointing outside are caught.
// With no allowed folders configured, any path is denied (fail closed).
class FolderSafetyGuard : public SafetyGuard {
public:
    explicit FolderSafetyGuard(const std::vector<std::string>& allowedFolders);

    SafetyDecision check(RiskLevel risk, const std::vector<std::string>& paths) const override;
    bool isAllowedPath(const std::string& path) const;

private:
    std::vector<std::filesystem::path> allowed_;
};

}  // namespace easy::core
