// Minimal tests until a test framework (GoogleTest) is added.
// Including every header makes sure the interfaces keep compiling.
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>

#include "actions/action.h"
#include "actions/list_files.h"
#include "actions/move_files.h"
#include "actions/path_utils.h"
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

    // --- matchesPattern ---
    using easy::actions::matchesPattern;
    check(matchesPattern("bill.pdf", "*.pdf"), "*.pdf matches bill.pdf");
    check(matchesPattern("Bill.PDF", "*.pdf"), "pattern is case-insensitive");
    check(!matchesPattern("bill.txt", "*.pdf"), "*.pdf does not match .txt");
    check(matchesPattern("a1.txt", "a?.txt"), "? matches one character");
    check(!matchesPattern("a12.txt", "a?.txt"), "? does not match two characters");
    check(matchesPattern("anything", "*"), "* matches everything");
    check(matchesPattern("report_2025_final.pdf", "report*final*"), "multiple *");

    // --- expandHome ---
    using easy::actions::expandHome;
    check(expandHome("/tmp/x") == "/tmp/x", "absolute path unchanged");
    check(expandHome("~/Downloads").find('~') == std::string::npos, "~ expanded");
    check(expandHome("~/Downloads").size() > std::string("/Downloads").size(), "home prefix added");

    // --- list_files: uses a temporary folder, never real user files ---
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() /
                             ("easy_test_" + std::to_string(std::random_device{}()));
        fs::create_directories(dir / "subfolder");
        for (const char* name : {"a.pdf", "B.PDF", "c.txt", ".hidden.pdf"}) {
            std::ofstream(dir / name) << "test";
        }

        easy::actions::ListFilesAction list;
        check(list.name() == "list_files", "list_files name");
        check(list.risk() == easy::core::RiskLevel::Safe, "list_files is Safe");

        auto res = list.run({{"folder", dir.string()}, {"pattern", "*.pdf"}});
        check(res.ok, "list_files succeeds");
        check(res.outputs.size() == 2, "finds 2 pdfs (case-insensitive, skips hidden)");

        res = list.run({{"folder", dir.string()}});
        check(res.ok && res.outputs.size() == 3, "no pattern lists all visible files, not folders");

        res = list.run({{"folder", (dir / "missing").string()}});
        check(!res.ok, "missing folder gives error");

        res = list.run({});
        check(!res.ok, "missing folder param gives error");

        std::error_code ec;
        fs::remove_all(dir, ec);   // clean up
    }

    // --- move_files: also in a temporary folder ---
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() /
                             ("easy_test_" + std::to_string(std::random_device{}()));
        const fs::path src = dir / "src";
        const fs::path dest = dir / "dest";   // does not exist yet
        fs::create_directories(src);
        for (const char* name : {"a.pdf", "b.pdf"}) std::ofstream(src / name) << "test";

        const std::string both = (src / "a.pdf").string() + "\n" + (src / "b.pdf").string();
        easy::actions::MoveFilesAction move;
        check(move.name() == "move_files", "move_files name");
        check(move.risk() == easy::core::RiskLevel::Confirm, "move_files needs confirmation");

        // Normal move: destination folder is created, files arrive, verify passes
        easy::actions::Params p{{"files", both}, {"to", dest.string()}};
        auto res = move.run(p);
        check(res.ok, "move_files succeeds");
        check(res.outputs.size() == 2, "two files reported moved");
        check(fs::exists(dest / "a.pdf") && fs::exists(dest / "b.pdf"), "files arrived");
        check(!fs::exists(src / "a.pdf") && !fs::exists(src / "b.pdf"), "files left source");
        check(move.verify(p), "verify passes after move");

        // Conflict: a.pdf already exists in dest -> nothing moved at all
        std::ofstream(src / "a.pdf") << "new";
        std::ofstream(src / "c.pdf") << "new";
        const std::string conflict = (src / "c.pdf").string() + "\n" + (src / "a.pdf").string();
        res = move.run({{"files", conflict}, {"to", dest.string()}});
        check(!res.ok, "conflict is an error");
        check(fs::exists(src / "c.pdf"), "nothing moved when one file conflicts");

        // Missing source file -> error, nothing moved
        res = move.run({{"files", (src / "nope.pdf").string()}, {"to", dest.string()}});
        check(!res.ok, "missing source file is an error");

        // Bad params
        check(!move.run({{"to", dest.string()}}).ok, "missing files param is an error");
        check(!move.run({{"files", both}}).ok, "missing to param is an error");

        // Empty list (e.g. list_files found nothing) is fine
        res = move.run({{"files", ""}, {"to", dest.string()}});
        check(res.ok && res.outputs.empty(), "empty list moves nothing, ok");

        std::error_code ec;
        fs::remove_all(dir, ec);   // clean up
    }

    if (failures == 0) std::cout << "All tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
