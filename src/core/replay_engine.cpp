#include "core/replay_engine.h"

#include <algorithm>
#include <map>
#include <ostream>
#include <vector>

#include "actions/action.h"
#include "actions/registry.h"
#include "core/confirmer.h"
#include "core/safety_guard.h"

namespace easy::core {

namespace {

using Variables = std::map<std::string, std::vector<std::string>>;

std::string joinLines(const std::vector<std::string>& items) {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) out += '\n';
        out += items[i];
    }
    return out;
}

// A param whose whole value is "$name" is replaced by the saved list
// (one item per line). Returns false and sets `error` for unknown names.
bool resolveVariables(const actions::Params& raw, const Variables& vars,
                      actions::Params& resolved, std::string& error) {
    resolved.clear();
    for (const auto& [key, value] : raw) {
        if (value.size() > 1 && value[0] == '$') {
            const auto it = vars.find(value.substr(1));
            if (it == vars.end()) {
                error = "unknown variable " + value + " (no earlier step saved it)";
                return false;
            }
            resolved[key] = joinLines(it->second);
        } else {
            resolved[key] = value;
        }
    }
    return true;
}

// Short, readable summary of params for the confirmation question:
//   files=(2 items)  to=/Users/you/Documents/Bills
std::string describe(const actions::Params& params) {
    std::string out;
    for (const auto& [key, value] : params) {
        out += "  " + key + "=";
        const auto lines = 1 + std::count(value.begin(), value.end(), '\n');
        if (lines > 1) out += "(" + std::to_string(lines) + " items)";
        else out += value;
    }
    return out;
}

}  // namespace

SimpleReplayEngine::SimpleReplayEngine(const actions::ActionRegistry& registry,
                                       const SafetyGuard& guard, Confirmer& confirmer,
                                       std::ostream& log)
    : registry_(registry), guard_(guard), confirmer_(confirmer), log_(log) {}

ReplayResult SimpleReplayEngine::replay(Recipe& recipe) {
    ReplayResult result;
    Variables vars;

    auto stop = [&](int stepNo, const std::string& why) {
        result.ok = false;
        result.message = "step " + std::to_string(stepNo) + ": " + why;
        log_ << "  x " << result.message << "\n";
        return result;
    };

    for (std::size_t i = 0; i < recipe.steps.size(); ++i) {
        const Step& step = recipe.steps[i];
        const int stepNo = static_cast<int>(i) + 1;

        // 1. find the action
        actions::Action* action = registry_.find(step.action);
        if (action == nullptr) return stop(stepNo, "unknown action \"" + step.action + "\"");

        // 2. fill in variables
        actions::Params params;
        std::string error;
        if (!resolveVariables(step.params, vars, params, error)) return stop(stepNo, error);

        // 3. safety guard
        std::vector<std::string> paths;
        for (const std::string& name : action->pathParams()) {
            const auto it = params.find(name);
            if (it != params.end()) paths.push_back(it->second);
        }
        const SafetyDecision safety = guard_.check(action->riskFor(params), paths);
        if (safety.decision == Decision::Deny) return stop(stepNo, "denied: " + safety.reason);
        if (safety.decision == Decision::AskUser) {
            const std::string question =
                "Step " + std::to_string(stepNo) + ": " + step.action + describe(params) +
                "\nRun this step?";
            if (!confirmer_.confirm(question)) return stop(stepNo, "cancelled by user");
        }

        // 4. run
        actions::ActionResult run = action->run(params);
        if (!run.ok) return stop(stepNo, run.message);

        // 5. verify
        if (!action->verify(params)) return stop(stepNo, "verification failed");

        // 6. save outputs
        if (!step.save_as.empty()) vars[step.save_as] = std::move(run.outputs);

        ++result.steps_done;
        log_ << "  ok step " << stepNo << " " << step.action << ": " << run.message << "\n";
    }

    result.ok = true;
    result.message = "done: " + std::to_string(result.steps_done) + " step(s)";
    return result;
}

}  // namespace easy::core
