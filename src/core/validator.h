#pragma once

#include <string>
#include <vector>

#include "core/recipe.h"

namespace easy::actions {
class ActionRegistry;
}

// Checks a recipe (especially one written by the AI) before it is shown or run:
//   - every action exists in the registry
//   - every "$name" is saved by an EARLIER step
//   - save_as names are not reused
// Path permissions are checked later, per step, by the SafetyGuard
// (paths are only known after "$name" values are filled in).
namespace easy::core {

struct ValidationResult {
    bool ok = false;
    std::vector<std::string> errors;
};

class Validator {
public:
    virtual ~Validator() = default;
    virtual ValidationResult validate(const Recipe& recipe) const = 0;
};

class RecipeValidator : public Validator {
public:
    explicit RecipeValidator(const actions::ActionRegistry& registry) : registry_(registry) {}
    ValidationResult validate(const Recipe& recipe) const override;

private:
    const actions::ActionRegistry& registry_;
};

}  // namespace easy::core
