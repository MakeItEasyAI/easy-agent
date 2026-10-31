#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "actions/path_utils.h"
#include "storage/recipe_json.h"
#include "storage/storage.h"

namespace easy::storage {

namespace fs = std::filesystem;

std::string defaultRecipesDir() {
    return actions::expandHome("~/.makeiteasy/recipes");
}

bool isValidRecipeId(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    return std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '-';
    });
}

std::string slugify(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        if (std::isalnum(c) && c < 128) {
            out += static_cast<char>(std::tolower(c));
        } else if (!out.empty() && out.back() != '_') {
            out += '_';
        }
        if (out.size() >= 40) break;
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? "recipe" : out;
}

std::string normalizeTask(const std::string& task) {
    std::string out;
    for (unsigned char c : task) {
        if (std::isspace(c)) {
            if (!out.empty() && out.back() != ' ') out += ' ';
        } else {
            out += static_cast<char>(std::tolower(c));
        }
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.' || out.back() == '!' ||
                            out.back() == '?')) {
        out.pop_back();
    }
    return out;
}

std::string FileRecipeStore::save(core::Recipe& recipe) {
    if (!isValidRecipeId(recipe.id)) recipe.id = slugify(recipe.task.empty() ? recipe.id : recipe.task);

    std::error_code ec;
    fs::create_directories(folder_, ec);
    if (ec) return "cannot create " + folder_ + ": " + ec.message();

    // Pick a free name: id, id_2, id_3, ...
    const std::string base = recipe.id;
    for (int n = 2; fs::exists(fs::path(folder_) / (recipe.id + ".json"), ec); ++n) {
        if (n > 999) return "too many recipes named " + base;
        recipe.id = base + "_" + std::to_string(n);
    }
    return writeFile(recipe);
}

std::string FileRecipeStore::replace(core::Recipe& recipe) {
    if (!isValidRecipeId(recipe.id)) return "not a valid recipe name: " + recipe.id;
    std::error_code ec;
    if (!fs::exists(fs::path(folder_) / (recipe.id + ".json"), ec)) return save(recipe);
    return writeFile(recipe);
}

std::string FileRecipeStore::writeFile(const core::Recipe& recipe) {
    std::error_code ec;
    fs::create_directories(folder_, ec);
    if (ec) return "cannot create " + folder_ + ": " + ec.message();

    // Write to a temporary file first, then rename: a crash never leaves a
    // half-written recipe behind.
    const fs::path target = fs::path(folder_) / (recipe.id + ".json");
    const fs::path temp = fs::path(folder_) / ("." + recipe.id + ".json.tmp");
    {
        std::ofstream out(temp, std::ios::trunc);
        if (!out) return "cannot write " + temp.string();
        out << recipeToJson(recipe);
        if (!out) return "cannot write " + temp.string();
    }
    fs::rename(temp, target, ec);
    if (ec) {
        fs::remove(temp, ec);
        return "cannot save " + target.string();
    }
    return "";
}

std::optional<core::Recipe> FileRecipeStore::load(const std::string& id) {
    if (!isValidRecipeId(id)) return std::nullopt;
    const auto result = loadRecipeFile((fs::path(folder_) / (id + ".json")).string());
    return result.recipe;
}

std::vector<core::Recipe> FileRecipeStore::list() {
    std::vector<core::Recipe> recipes;
    std::error_code ec;
    if (!fs::is_directory(folder_, ec)) return recipes;

    for (fs::directory_iterator it(folder_, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path& p = it->path();
        if (p.extension() != ".json" || p.filename().string()[0] == '.') continue;
        auto result = loadRecipeFile(p.string());
        if (result.recipe) recipes.push_back(std::move(*result.recipe));   // skip broken files
    }
    std::sort(recipes.begin(), recipes.end(),
              [](const core::Recipe& a, const core::Recipe& b) { return a.id < b.id; });
    return recipes;
}

std::optional<core::Recipe> FileRecipeStore::findByTask(const std::string& task) {
    const std::string wanted = normalizeTask(task);
    if (wanted.empty()) return std::nullopt;
    for (core::Recipe& recipe : list()) {
        if (normalizeTask(recipe.task) == wanted) return std::move(recipe);
    }
    return std::nullopt;
}

}  // namespace easy::storage
