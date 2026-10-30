#pragma once

#include "core/recipe.h"

// Every step passes through the safety guard before it runs.
namespace easy::core {

enum class RiskLevel {
    Safe,      // run directly (list files, open app)
    Confirm,   // ask the user (move/delete files, submit forms)
    Blocked    // never run (passwords, sudo, outside allowed folders)
};

enum class Decision { Allow, AskUser, Deny };

class SafetyGuard {
public:
    virtual ~SafetyGuard() = default;
    virtual Decision check(const Step& step) const = 0;
};

}  // namespace easy::core
