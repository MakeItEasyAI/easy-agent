#include "core/validator.h"

#include <set>

#include "actions/registry.h"

namespace easy::core {

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
        if (!step.save_as.empty() && !saved.insert(step.save_as).second) {
            result.errors.push_back(where + "save_as \"" + step.save_as + "\" is used twice");
        }
    }

    result.ok = result.errors.empty();
    return result;
}

}  // namespace easy::core
