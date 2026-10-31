#include "storage/recipe_json.h"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace easy::storage {

namespace {

using nlohmann::json;

RecipeLoadResult fail(const std::string& message) {
    return {std::nullopt, message};
}

}  // namespace

RecipeLoadResult parseRecipe(const std::string& jsonText) {
    // allow_exceptions = false: bad JSON gives a "discarded" value instead of throwing.
    const json j = json::parse(jsonText, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) return fail("invalid JSON");
    if (!j.is_object()) return fail("recipe must be a JSON object");

    core::Recipe recipe;

    if (!j.contains("id") || !j["id"].is_string() || j["id"].get<std::string>().empty())
        return fail("missing or empty \"id\"");
    recipe.id = j["id"].get<std::string>();

    if (j.contains("version")) {
        if (!j["version"].is_number_integer()) return fail("\"version\" must be an integer");
        recipe.version = j["version"].get<int>();
    }

    if (j.contains("task")) {
        if (!j["task"].is_string()) return fail("\"task\" must be a string");
        recipe.task = j["task"].get<std::string>();
    }

    if (!j.contains("steps") || !j["steps"].is_array() || j["steps"].empty())
        return fail("\"steps\" must be a non-empty array");

    for (std::size_t i = 0; i < j["steps"].size(); ++i) {
        const json& s = j["steps"][i];
        const std::string where = "step " + std::to_string(i + 1) + ": ";

        if (!s.is_object()) return fail(where + "must be an object");
        if (!s.contains("action") || !s["action"].is_string() ||
            s["action"].get<std::string>().empty())
            return fail(where + "missing \"action\"");

        core::Step step;
        step.action = s["action"].get<std::string>();

        if (s.contains("params")) {
            if (!s["params"].is_object()) return fail(where + "\"params\" must be an object");
            for (const auto& [key, value] : s["params"].items()) {
                if (!value.is_string())
                    return fail(where + "param \"" + key + "\" must be a string");
                step.params[key] = value.get<std::string>();
            }
        }

        if (s.contains("save_as")) {
            if (!s["save_as"].is_string()) return fail(where + "\"save_as\" must be a string");
            step.save_as = s["save_as"].get<std::string>();
        }

        recipe.steps.push_back(std::move(step));
    }

    return {std::move(recipe), ""};
}

RecipeLoadResult loadRecipeFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) return fail("cannot open file: " + path);

    std::stringstream buffer;
    buffer << file.rdbuf();
    RecipeLoadResult result = parseRecipe(buffer.str());
    if (!result.recipe) result.error = path + ": " + result.error;
    return result;
}

}  // namespace easy::storage
