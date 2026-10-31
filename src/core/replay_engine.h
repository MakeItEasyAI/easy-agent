#pragma once

#include <iosfwd>
#include <string>

#include "core/recipe.h"

namespace easy::actions {
class ActionRegistry;
}

// Runs a recipe step by step:
//   safety check -> run action -> verify -> (on failure) retry -> self-heal
// Never calls the AI unless a step breaks.
namespace easy::core {

class SafetyGuard;
class Confirmer;

struct ReplayResult {
    bool ok = false;
    bool recipe_changed = false;   // true if self-heal updated a step
    int steps_done = 0;            // how many steps finished successfully
    std::string message;
};

class ReplayEngine {
public:
    virtual ~ReplayEngine() = default;
    virtual ReplayResult replay(Recipe& recipe) = 0;
};

// v0.1 engine: no retry, no self-heal yet (v0.5). Stops at the first problem.
// For each step:
//   1. find the action by name          (unknown -> stop)
//   2. replace "$name" params with saved results
//   3. safety guard                     (Deny -> stop, AskUser -> confirm)
//   4. run the action                   (fail -> stop)
//   5. verify                           (fail -> stop)
//   6. save outputs if the step has save_as
class SimpleReplayEngine : public ReplayEngine {
public:
    SimpleReplayEngine(const actions::ActionRegistry& registry, const SafetyGuard& guard,
                       Confirmer& confirmer, std::ostream& log);

    ReplayResult replay(Recipe& recipe) override;

private:
    const actions::ActionRegistry& registry_;
    const SafetyGuard& guard_;
    Confirmer& confirmer_;
    std::ostream& log_;
};

}  // namespace easy::core
