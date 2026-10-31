#pragma once

#include <optional>
#include <string>

#include "core/recipe.h"

// Converts recipe JSON (text or file) into a core::Recipe.
// Never throws: on failure, `recipe` is empty and `error` explains why.
namespace easy::storage {

struct RecipeLoadResult {
    std::optional<core::Recipe> recipe;
    std::string error;
};

RecipeLoadResult parseRecipe(const std::string& jsonText);
RecipeLoadResult loadRecipeFile(const std::string& path);

// The reverse: Recipe -> pretty JSON text (same format parseRecipe reads).
std::string recipeToJson(const core::Recipe& recipe);

}  // namespace easy::storage
