#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "actions/action.h"

// Finds an action by its name, e.g. "move_files" -> MoveFilesAction.
// Only actions registered here can ever run: an unknown name in a recipe
// (typo, or something the AI invented) is rejected.
namespace easy::actions {

class AiAccess;

class ActionRegistry {
public:
    void add(std::unique_ptr<Action> action);
    Action* find(const std::string& name) const;   // nullptr if not registered

    // All registered actions, sorted by name (used to describe them to the AI).
    std::vector<const Action*> all() const;

private:
    std::map<std::string, std::unique_ptr<Action>> actions_;
};

// Registry with all built-in actions (files, apps/URLs, ui_*, read_pdf, ai_answer).
// allowedApps: apps that open_app may start without asking (from config).
// ai:          how ai_answer reaches the AI model (null = ai_answer reports no AI).
ActionRegistry makeDefaultRegistry(const std::vector<std::string>& allowedApps = {},
                                   std::shared_ptr<AiAccess> ai = nullptr);

}  // namespace easy::actions
