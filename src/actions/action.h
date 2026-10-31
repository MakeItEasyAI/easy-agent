#pragma once

#include <map>
#include <string>
#include <vector>

#include "core/safety_guard.h"

// A single unit of work the agent can perform. Implementations (planned):
//   open_app, open_url, list_files, move_files, rename_file, click, type
namespace easy::actions {

using Params = std::map<std::string, std::string>;

struct ActionResult {
    bool ok = false;
    std::string message;
    std::vector<std::string> outputs;   // e.g. file paths; stored by `save_as`
};

class Action {
public:
    virtual ~Action() = default;
    virtual std::string name() const = 0;

    // One-line explanation + params, shown to the AI so it knows what it can use.
    virtual std::string description() const = 0;

    virtual core::RiskLevel risk() const = 0;

    // Risk for these exact params. Default: same as risk(). Lets an action be
    // Safe for some values (e.g. an app you allowed) and Confirm for others.
    virtual core::RiskLevel riskFor(const Params& /*params*/) const { return risk(); }

    // Quick check of the params BEFORE anything runs (used by the Validator on
    // AI-written recipes). Returns "" if fine, else a short reason that is sent
    // back to the AI so it can fix the recipe. Params that are "$name"
    // variables are not known yet and are skipped by the caller.
    virtual std::string checkParams(const Params& /*params*/) const { return ""; }

    // Name of an action that must appear EARLIER in the same recipe for this
    // one to make sense (e.g. browser_fill needs browser_open). "" = none.
    // Checked by the Validator, so the AI is told to fix the order.
    virtual std::string needsEarlier() const { return ""; }

    // Names of params that hold file/folder paths. The safety guard checks
    // every one of them against the allowed folders before the action runs.
    // Pure virtual on purpose: a new action cannot forget to declare them.
    virtual std::vector<std::string> pathParams() const = 0;

    virtual ActionResult run(const Params& params) = 0;
    virtual bool verify(const Params& params) = 0;   // did it actually work?
};

}  // namespace easy::actions
