#pragma once

#include <string>
#include <vector>

#include "core/recipe.h"

// Checks every recipe (especially ones produced by the AI) before it runs:
// actions exist, params are present, paths are inside allowed folders.
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

}  // namespace easy::core
