#include "cli/cli.h"

#include <iostream>
#include <string>

#include "actions/registry.h"
#include "cli/console_confirmer.h"
#include "core/planner.h"
#include "core/replay_engine.h"
#include "core/safety_guard.h"
#include "core/validator.h"
#include "models/gemini_provider.h"
#include "storage/config.h"
#include "storage/recipe_json.h"

namespace easy::cli {

namespace {

void printUsage() {
    std::cout << "easy - teach your computer a task once, run it forever\n\n"
                 "Usage:\n"
                 "  easy show <recipe.json>         print the steps of a recipe\n"
                 "  easy run <recipe.json>          run a recipe (asks before risky steps)\n"
                 "       [--config <config.json>]   default: ~/.makeiteasy/config.json\n"
                 "  easy plan \"<task>\"             ask the AI for a recipe and show it\n"
                 "                                  (does not run or save; needs GEMINI_API_KEY)\n"
                 "  easy learn \"<task>\"            plan, run and save (not implemented)\n"
                 "  easy list                       list saved recipes (not implemented)\n"
                 "  easy schedule <recipe> \"<when>\" schedule a recipe (not implemented)\n"
                 "  easy --version                  print version\n";
}

void printRecipe(const core::Recipe& r) {
    std::cout << "Recipe: " << r.id << " (v" << r.version << ")\n";
    if (!r.task.empty()) std::cout << "Task:   " << r.task << "\n";
    std::cout << "Steps (" << r.steps.size() << "):\n";

    int n = 1;
    for (const auto& step : r.steps) {
        std::cout << "  " << n++ << ". " << step.action;
        for (const auto& [key, value] : step.params) {
            std::cout << "  " << key << "=" << value;
        }
        if (!step.save_as.empty()) std::cout << "  -> $" << step.save_as;
        std::cout << "\n";
    }
}

// easy plan "<task>": ask the AI for a recipe and print it. Runs nothing.
int planCommand(int argc, char** argv) {
    if (argc < 3 || std::string(argv[2]).empty()) {
        std::cerr << "Usage: easy plan \"<task>\"\n";
        return 1;
    }
    const std::string task = argv[2];

    std::string error;
    auto model = models::makeGeminiFromEnv(error);
    if (!model) {
        std::cerr << "Error: " << error << "\n";
        if (error.find("GEMINI_API_KEY") != std::string::npos) {
            std::cerr << "\nGet a free key at https://aistudio.google.com, then in this terminal:\n"
                         "  export GEMINI_API_KEY=\"<your key>\"\n"
                         "Never put the key in a file inside the project.\n";
        }
        return 1;
    }

    const auto registry = actions::makeDefaultRegistry();
    const core::RecipeValidator validator(registry);
    core::LlmPlanner planner(*model, registry, validator);

    std::cout << "Asking " << model->name() << " (your task text is sent to Google)...\n";
    const core::PlanResult result = planner.plan(task);
    if (!result.recipe) {
        std::cerr << "Could not plan: " << result.error << "\n";
        return 1;
    }

    printRecipe(*result.recipe);
    std::cout << "\nNothing was run. (" << result.attempts << " AI call(s))\n";
    return 0;
}

void printConfigHelp(const std::string& path) {
    std::cerr << "\nThe agent only touches folders you allow. Create " << path
              << " like this:\n\n"
                 "  {\n"
                 "    \"allowed_folders\": [\"~/Downloads\", \"~/Documents\"]\n"
                 "  }\n";
}

// easy run <recipe.json> [--config <path>]
int runCommand(int argc, char** argv) {
    std::string recipePath;
    std::string configPath = storage::defaultConfigPath();

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --config needs a file path\n";
                return 1;
            }
            configPath = argv[++i];
        } else if (recipePath.empty()) {
            recipePath = arg;
        } else {
            std::cerr << "Error: unexpected argument: " << arg << "\n";
            return 1;
        }
    }
    if (recipePath.empty()) {
        std::cerr << "Usage: easy run <recipe.json> [--config <config.json>]\n";
        return 1;
    }

    const auto recipeResult = storage::loadRecipeFile(recipePath);
    if (!recipeResult.recipe) {
        std::cerr << "Error: " << recipeResult.error << "\n";
        return 1;
    }

    const auto configResult = storage::loadConfigFile(configPath);
    if (!configResult.config) {
        std::cerr << "Error: " << configResult.error << "\n";
        printConfigHelp(configPath);
        return 1;
    }

    core::Recipe recipe = *recipeResult.recipe;
    const auto registry = actions::makeDefaultRegistry();
    const core::FolderSafetyGuard guard(configResult.config->allowed_folders);
    ConsoleConfirmer confirmer;
    core::SimpleReplayEngine engine(registry, guard, confirmer, std::cout);

    std::cout << "Running " << recipe.id << " (" << recipe.steps.size() << " steps)\n";
    const core::ReplayResult result = engine.replay(recipe);

    if (!result.ok) {
        std::cerr << "Stopped: " << result.message << "\n";
        return 1;
    }
    std::cout << "Done: " << recipe.id << " (" << result.steps_done << " steps)\n";
    return 0;
}

// easy show <path>: load a recipe file and print it in a readable form.
int showCommand(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: easy show <recipe.json>\n";
        return 1;
    }

    const auto result = storage::loadRecipeFile(argv[2]);
    if (!result.recipe) {
        std::cerr << "Error: " << result.error << "\n";
        return 1;
    }

    printRecipe(*result.recipe);
    return 0;
}

}  // namespace

int run(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return 0;
    }

    const std::string cmd = argv[1];
    if (cmd == "--version" || cmd == "-v") {
        std::cout << "easy " << EASY_VERSION << "\n";
        return 0;
    }
    if (cmd == "--help" || cmd == "-h") {
        printUsage();
        return 0;
    }
    if (cmd == "show") {
        return showCommand(argc, argv);
    }
    if (cmd == "run") {
        return runCommand(argc, argv);
    }
    if (cmd == "plan") {
        return planCommand(argc, argv);
    }

    std::cerr << "Unknown or not-yet-implemented command: " << cmd << "\n";
    printUsage();
    return 1;
}

}  // namespace easy::cli
