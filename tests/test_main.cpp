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

    if (failures == 0) std::cout << "All tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
