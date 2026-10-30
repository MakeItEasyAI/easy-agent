#pragma once

// Command-line interface: parses `easy <command> ...` and dispatches.
// Commands (planned): learn, run, list, show, schedule, logs, setup.
namespace easy::cli {

int run(int argc, char** argv);

}  // namespace easy::cli
