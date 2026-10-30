#pragma once

#include <string>

#include "core/recipe.h"

// Runs a recipe step by step:
//   safety check -> run action -> verify -> (on failure) retry -> self-heal
// Never calls the AI unless a step breaks.
namespace easy::core {

struct ReplayResult {
    bool ok = false;
    bool recipe_changed = false;   // true if self-heal updated a step
    std::string message;
};

class ReplayEngine {
public:
    virtual ~ReplayEngine() = default;
    virtual ReplayResult replay(Recipe& recipe) = 0;
};

}  // namespace easy::core
