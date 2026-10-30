#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/recipe.h"

// Reads and writes agent data under ~/.makeiteasy/
//   recipes/*.json, schedules.json, logs/
namespace easy::storage {

class RecipeStore {
public:
    virtual ~RecipeStore() = default;
    virtual bool save(const core::Recipe& recipe) = 0;
    virtual std::optional<core::Recipe> load(const std::string& id) = 0;
    virtual std::vector<std::string> list() = 0;
};

}  // namespace easy::storage
