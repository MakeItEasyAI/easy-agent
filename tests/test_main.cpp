// Minimal tests until a test framework (GoogleTest) is added.
// Including every header makes sure the interfaces keep compiling.
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>

#include "actions/action.h"
#include "actions/list_files.h"
#include "actions/move_files.h"
#include "actions/registry.h"
#include "cli/console_confirmer.h"
#include "core/confirmer.h"
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

    // --- pathParams: every action declares its path params ---
    check(easy::actions::ListFilesAction{}.pathParams() == std::vector<std::string>{"folder"},
          "list_files declares folder as a path");
    check(easy::actions::MoveFilesAction{}.pathParams() ==
              std::vector<std::string>{"files", "to"},
          "move_files declares files and to as paths");

    // --- FolderSafetyGuard ---
    {
        namespace fs = std::filesystem;
        using easy::core::Decision;
        using easy::core::RiskLevel;

        const fs::path dir = fs::temp_directory_path() /
                             ("easy_test_" + std::to_string(std::random_device{}()));
        const fs::path allowed = dir / "allowed";
        const fs::path outside = dir / "outside";
        fs::create_directories(allowed);
        fs::create_directories(outside);
        fs::create_directories(dir / "allowed_evil");

        easy::core::FolderSafetyGuard guard({allowed.string()});
        const std::string inFile = (allowed / "a.pdf").string();
        const std::string outFile = (outside / "a.pdf").string();

        check(guard.check(RiskLevel::Safe, {inFile}).decision == Decision::Allow,
              "safe action inside allowed folder is allowed");
        check(guard.check(RiskLevel::Confirm, {inFile}).decision == Decision::AskUser,
              "confirm action inside allowed folder asks user");
        check(guard.check(RiskLevel::Blocked, {inFile}).decision == Decision::Deny,
              "blocked action is always denied");
        check(guard.check(RiskLevel::Safe, {outFile}).decision == Decision::Deny,
              "path outside allowed folders is denied");
        check(guard.check(RiskLevel::Safe, {inFile + "\n" + outFile}).decision == Decision::Deny,
              "one bad path in a multi-line list denies the whole step");
        check(guard.check(RiskLevel::Safe, {}).decision == Decision::Allow,
              "safe action with no paths is allowed");

        // Tricks
        check(!guard.isAllowedPath((allowed / ".." / "outside" / "a.pdf").string()),
              "'..' escape is denied");
        check(!guard.isAllowedPath((dir / "allowed_evil" / "a.pdf").string()),
              "similar folder name prefix is denied");
        check(guard.isAllowedPath(allowed.string()), "the allowed folder itself is allowed");
        check(guard.isAllowedPath((allowed / "new" / "sub" / "x.pdf").string()),
              "not-yet-existing path inside allowed folder is allowed");
        check(!guard.isAllowedPath(""), "empty path is denied");

        std::error_code linkEc;
        fs::create_directory_symlink(outside, allowed / "link", linkEc);
        if (!linkEc) {   // creating links can need extra rights on Windows; skip there
            check(!guard.isAllowedPath((allowed / "link" / "a.pdf").string()),
                  "symlink pointing outside is denied");
        }

        // Fail closed: no allowed folders -> any path denied
        easy::core::FolderSafetyGuard empty({});
        check(empty.check(RiskLevel::Safe, {inFile}).decision == Decision::Deny,
              "no allowed folders means paths are denied");

        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    // --- ConsoleConfirmer: only y/yes means yes ---
    {
        auto answer = [](const std::string& input) {
            std::istringstream in(input);
            std::ostringstream out;
            easy::cli::ConsoleConfirmer confirmer(in, out);
            return confirmer.confirm("Move 2 files?");
        };
        check(answer("y\n"), "y is yes");
        check(answer("  YES \n"), "YES with spaces is yes");
        check(!answer("n\n"), "n is no");
        check(!answer("\n"), "empty answer is no");
        check(!answer(""), "no input is no");
        check(!answer("yep\n"), "anything else is no");
    }

    // --- Replay engine: full recipe, end to end, in a temporary folder ---
    {
        namespace fs = std::filesystem;
        using easy::core::Recipe;
        using easy::core::Step;

        // Fake confirmer: answers automatically and remembers it was asked
        struct FakeConfirmer : easy::core::Confirmer {
            bool answer = true;
            int asked = 0;
            bool confirm(const std::string&) override { ++asked; return answer; }
        };

        const fs::path dir = fs::temp_directory_path() /
                             ("easy_test_" + std::to_string(std::random_device{}()));
        const fs::path downloads = dir / "Downloads";
        const fs::path bills = dir / "Bills";
        auto reset = [&] {
            std::error_code ec;
            fs::remove_all(dir, ec);
            fs::create_directories(downloads);
            for (const char* name : {"a.pdf", "b.pdf", "notes.txt"})
                std::ofstream(downloads / name) << "test";
        };

        auto makeRecipe = [&](const std::string& to) {
            Recipe r;
            r.id = "move_bills";
            r.steps.push_back(Step{"list_files",
                                   {{"folder", downloads.string()}, {"pattern", "*.pdf"}},
                                   "pdfs"});
            r.steps.push_back(Step{"move_files", {{"files", "$pdfs"}, {"to", to}}, ""});
            return r;
        };

        const auto registry = easy::actions::makeDefaultRegistry();
        easy::core::FolderSafetyGuard guard({dir.string()});
        std::ostringstream log;

        // 1. Happy path: user says yes -> PDFs moved, txt stays
        reset();
        {
            FakeConfirmer yes;
            easy::core::SimpleReplayEngine engine(registry, guard, yes, log);
            Recipe r = makeRecipe(bills.string());
            const auto res = engine.replay(r);
            check(res.ok, "replay succeeds");
            check(res.steps_done == 2, "both steps done");
            check(yes.asked == 1, "asked once (only move_files needs confirmation)");
            check(fs::exists(bills / "a.pdf") && fs::exists(bills / "b.pdf"), "pdfs moved");
            check(fs::exists(downloads / "notes.txt"), "non-pdf left alone");
        }

        // 2. User says no -> nothing moved
        reset();
        {
            FakeConfirmer no;
            no.answer = false;
            easy::core::SimpleReplayEngine engine(registry, guard, no, log);
            Recipe r = makeRecipe(bills.string());
            const auto res = engine.replay(r);
            check(!res.ok, "replay stops when user says no");
            check(res.steps_done == 1, "only list_files ran");
            check(fs::exists(downloads / "a.pdf"), "nothing moved after no");
        }

        // 3. Destination outside allowed folders -> denied, never asked
        reset();
        {
            FakeConfirmer yes;
            easy::core::FolderSafetyGuard narrow({downloads.string()});
            easy::core::SimpleReplayEngine engine(registry, narrow, yes, log);
            Recipe r = makeRecipe(bills.string());
            const auto res = engine.replay(r);
            check(!res.ok, "outside destination is denied");
            check(yes.asked == 0, "denied steps never ask the user");
            check(fs::exists(downloads / "a.pdf"), "nothing moved when denied");
        }

        // 4. Unknown action -> stop
        {
            FakeConfirmer yes;
            easy::core::SimpleReplayEngine engine(registry, guard, yes, log);
            Recipe r;
            r.id = "bad";
            r.steps.push_back(Step{"delete_everything", {}, ""});
            check(!engine.replay(r).ok, "unknown action is rejected");
        }

        // 5. Unknown variable -> stop
        reset();
        {
            FakeConfirmer yes;
            easy::core::SimpleReplayEngine engine(registry, guard, yes, log);
            Recipe r;
            r.id = "bad";
            r.steps.push_back(Step{"move_files", {{"files", "$nope"}, {"to", bills.string()}}, ""});
            const auto res = engine.replay(r);
            check(!res.ok, "unknown variable is rejected");
            check(yes.asked == 0, "unknown variable stops before asking");
        }

        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    if (failures == 0) std::cout << "All tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
