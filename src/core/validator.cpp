#include "core/validator.h"

#include <algorithm>
#include <cctype>
#include <set>

#include "actions/registry.h"

namespace easy::core {

namespace {

// Variable names: letters, digits, '_' (not starting with a digit).
bool isVariableName(const std::string& name) {
    if (name.empty() || std::isdigit(static_cast<unsigned char>(name[0]))) return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_';
    });
}

}  // namespace

ValidationResult RecipeValidator::validate(const Recipe& recipe) const {
    ValidationResult result;
    std::set<std::string> saved;   // names saved by earlier steps

    for (std::size_t i = 0; i < recipe.steps.size(); ++i) {
        const Step& step = recipe.steps[i];
        const std::string where = "step " + std::to_string(i + 1) + ": ";

        if (registry_.find(step.action) == nullptr) {
            result.errors.push_back(where + "unknown action \"" + step.action + "\"");
        }
        for (const auto& [key, value] : step.params) {
            if (value.size() > 1 && value[0] == '$' && saved.count(value.substr(1)) == 0) {
                result.errors.push_back(where + "param \"" + key + "\" uses " + value +
                                        " but no earlier step saves it");
            }
        }
        if (!step.save_as.empty() && !isVariableName(step.save_as)) {
            result.errors.push_back(where + "save_as \"" + step.save_as +
                                    "\" must use only letters, digits and _ (no $)");
        } else if (!step.save_as.empty() && !saved.insert(step.save_as).second) {
            result.errors.push_back(where + "save_as \"" + step.save_as + "\" is used twice");
        }
    }

    // A result that is saved but never used means the recipe stops half-way
    // (e.g. it lists files but forgets to move them).
    std::set<std::string> used;
    for (const Step& step : recipe.steps) {
        for (const auto& [key, value] : step.params) {
            if (value.size() > 1 && value[0] == '$') used.insert(value.substr(1));
        }
    }
    for (const std::string& name : saved) {
        if (used.count(name) == 0) {
            result.errors.push_back("\"" + name +
                                    "\" is saved but no later step uses it; the recipe looks "
                                    "unfinished (add the missing steps)");
        }
    }

    result.ok = result.errors.empty();
    return result;
}

}  // namespace easy::core
