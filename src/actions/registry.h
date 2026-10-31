#pragma once

#include <map>
#include <memory>
#include <string>

#include "actions/action.h"

// Finds an action by its name, e.g. "move_files" -> MoveFilesAction.
// Only actions registered here can ever run: an unknown name in a recipe
// (typo, or something the AI invented) is rejected.
namespace easy::actions {

class ActionRegistry {
public:
    void add(std::unique_ptr<Action> action);
    Action* find(const std::string& name) const;   // nullptr if not registered

private:
    std::map<std::string, std::unique_ptr<Action>> actions_;
};

// Registry with all built-in actions (list_files, move_files, ...).
ActionRegistry makeDefaultRegistry();

}  // namespace easy::actions
