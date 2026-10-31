// Minimal tests until a test framework (GoogleTest) is added.
// Including every header makes sure the interfaces keep compiling.
#include <cstdlib>
#include <iostream>

#include "actions/action.h"
#include "core/planner.h"
#include "core/recipe.h"
#include "core/replay_engine.h"
#include "core/safety_guard.h"
#include "core/scheduler.h"
#include "core/validator.h"
#include "models/model_provider.h"
#include "platform/platform.h"
#include "storage/recipe_json.h"
#include "storage/storage.h"

namespace {

int failures = 0;

void check(bool condition, const char* name) {
    if (!condition) {
        std::cerr << "FAIL: " << name << "\n";
        ++failures;
    }
}

}  // namespace

int main() {
    easy::core::Recipe recipe;
    check(recipe.version == 1, "new recipe starts at version 1");
    check(recipe.steps.empty(), "new recipe has no steps");

    easy::core::Step step{"list_files", {{"folder", "~/Downloads"}}, "pdfs"};
    recipe.steps.push_back(step);
    check(recipe.steps.size() == 1, "step added to recipe");
    check(recipe.steps[0].params.at("folder") == "~/Downloads", "step params stored");

    // --- parseRecipe: valid recipe ---
    {
        const auto res = easy::storage::parseRecipe(R"({
            "id": "move_bills", "version": 2, "task": "Move PDFs",
            "steps": [
              {"action": "list_files", "params": {"folder": "~/Downloads"}, "save_as": "pdfs"},
              {"action": "move_files", "params": {"files": "$pdfs", "to": "~/Bills"}}
            ]})");
        check(res.recipe.has_value(), "valid recipe parses");
        if (res.recipe) {
            check(res.recipe->id == "move_bills", "id parsed");
            check(res.recipe->version == 2, "version parsed");
            check(res.recipe->steps.size() == 2, "two steps parsed");
            check(res.recipe->steps[0].save_as == "pdfs", "save_as parsed");
            check(res.recipe->steps[1].params.at("files") == "$pdfs", "variable param kept");
        }
    }

    // --- parseRecipe: broken recipes return an error, never crash ---
    const char* bad[] = {
        "not json at all",
        "[]",                                                     // not an object
        R"({"steps": [{"action": "x"}]})",                        // no id
        R"({"id": "a"})",                                         // no steps
        R"({"id": "a", "steps": []})",                            // empty steps
        R"({"id": "a", "steps": [{"params": {}}]})",              // step without action
        R"({"id": "a", "steps": [{"action": "x", "params": {"n": 5}}]})",  // non-string param
    };
    for (const char* text : bad) {
        const auto res = easy::storage::parseRecipe(text);
        check(!res.recipe.has_value() && !res.error.empty(), text);
    }

    // --- loadRecipeFile: missing file ---
    {
        const auto res = easy::storage::loadRecipeFile("/no/such/file.json");
        check(!res.recipe.has_value(), "missing file gives error");
    }

    if (failures == 0) std::cout << "All tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
