#pragma once

#include <map>
#include <string>
#include <vector>

// A recipe is a saved task: an ordered list of steps that can be replayed
// without AI. See docs/design.md section 5 (Data model).
namespace easy::core {

struct Step {
    std::string action;                          // e.g. "list_files", "move_files"
    std::map<std::string, std::string> params;   // replaced by JSON later
    std::string save_as;                         // optional: name of output variable
};

struct Recipe {
    std::string id;           // e.g. "move_bills"
    int version = 1;          // bumped when self-heal changes a step
    std::string task;         // the original English request
    std::vector<Step> steps;
};

}  // namespace easy::core
