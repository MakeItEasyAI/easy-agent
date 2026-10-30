#include "cli/cli.h"

#include <iostream>
#include <string>

namespace easy::cli {

namespace {

void printUsage() {
    std::cout << "easy - teach your computer a task once, run it forever\n\n"
                 "Usage:\n"
                 "  easy learn \"<task>\"            plan a task with AI (not implemented)\n"
                 "  easy run <recipe>               replay a saved recipe (not implemented)\n"
                 "  easy list                       list saved recipes (not implemented)\n"
                 "  easy schedule <recipe> \"<when>\" schedule a recipe (not implemented)\n"
                 "  easy --version                  print version\n";
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

    std::cerr << "Unknown or not-yet-implemented command: " << cmd << "\n";
    printUsage();
    return 1;
}

}  // namespace easy::cli
