#pragma once

#include <optional>
#include <string>

#include "core/recipe.h"

// Uses an AI model only in two places:
//   1. learn: turn an English task into a recipe
//   2. fix:   repair a single broken step during self-heal
namespace easy::core {

class Planner {
public:
    virtual ~Planner() = default;
    virtual std::optional<Recipe> plan(const std::string& task) = 0;
    virtual std::optional<Step> fix(const Recipe& recipe,
                                    const Step& failed,
                                    const std::string& error) = 0;
};

}  // namespace easy::core
