#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/recipe.h"

// Saved recipes, one JSON file each: ~/.makeiteasy/recipes/<id>.json
namespace easy::storage {

class RecipeStore {
public:
    virtual ~RecipeStore() = default;

    // Saves the recipe. If another recipe already uses the id, a suffix is
    // added (_2, _3, ...) and recipe.id is updated. Returns "" or an error.
    virtual std::string save(core::Recipe& recipe) = 0;

    // Overwrites the saved recipe with the same id (used when the user fixed
    // it). Saves it as new if it does not exist yet. Returns "" or an error.
    virtual std::string replace(core::Recipe& recipe) = 0;

    virtual std::optional<core::Recipe> load(const std::string& id) = 0;
    virtual std::vector<core::Recipe> list() = 0;   // sorted by id

    // A saved recipe whose task matches (see normalizeTask), if any.
    virtual std::optional<core::Recipe> findByTask(const std::string& task) = 0;
};

class FileRecipeStore : public RecipeStore {
public:
    explicit FileRecipeStore(std::string folder) : folder_(std::move(folder)) {}

    std::string save(core::Recipe& recipe) override;
    std::string replace(core::Recipe& recipe) override;
    std::optional<core::Recipe> load(const std::string& id) override;
    std::vector<core::Recipe> list() override;
    std::optional<core::Recipe> findByTask(const std::string& task) override;

private:
    std::string writeFile(const core::Recipe& recipe);   // atomic: temp file + rename

    std::string folder_;
};

std::string defaultRecipesDir();   // ~/.makeiteasy/recipes, expanded

// Recipe ids become file names, so only letters, digits, '_' and '-' are
// allowed (max 64). This blocks names like "../../x" escaping the folder.
bool isValidRecipeId(const std::string& id);

// "Move PDFs to Bills!" -> "move_pdfs_to_bills" (fallback "recipe")
std::string slugify(const std::string& text);

// For matching tasks: lowercase, single spaces, no leading/trailing spaces,
// no trailing . ! ?   "  Move PDFs  to Bills. " -> "move pdfs to bills"
std::string normalizeTask(const std::string& task);

}  // namespace easy::storage
