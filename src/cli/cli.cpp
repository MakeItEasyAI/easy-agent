#include "cli/cli.h"

#include <iostream>
#include <string>

#include "storage/recipe_json.h"

namespace easy::cli {

namespace {

void printUsage() {
    std::cout << "easy - teach your computer a task once, run it forever\n\n"
                 "Usage:\n"
                 "  easy show <recipe.json>         print the steps of a recipe\n"
                 "  easy learn \"<task>\"            plan a task with AI (not implemented)\n"
                 "  easy run <recipe>               replay a saved recipe (not implemented)\n"
                 "  easy list                       list saved recipes (not implemented)\n"
                 "  easy schedule <recipe> \"<when>\" schedule a recipe (not implemented)\n"
                 "  easy --version                  print version\n";
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

    const core::Recipe& r = *result.recipe;
    std::cout << "Recipe: " << r.id << " (v" << r.version << ")\n";
    if (!r.task.empty()) std::cout << "Task:   " << r.task << "\n";
    std::cout << "Steps:\n";

    int n = 1;
    for (const auto& step : r.steps) {
        std::cout << "  " << n++ << ". " << step.action;
        for (const auto& [key, value] : step.params) {
            std::cout << "  " << key << "=" << value;
        }
        if (!step.save_as.empty()) std::cout << "  -> $" << step.save_as;
        std::cout << "\n";
    }
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

    std::cerr << "Unknown or not-yet-implemented command: " << cmd << "\n";
    printUsage();
    return 1;
}

}  // namespace easy::cli
