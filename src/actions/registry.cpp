#include "actions/registry.h"

#include "actions/list_files.h"
#include "actions/move_files.h"

namespace easy::actions {

void ActionRegistry::add(std::unique_ptr<Action> action) {
    const std::string actionName = action->name();
    actions_[actionName] = std::move(action);
}

Action* ActionRegistry::find(const std::string& name) const {
    const auto it = actions_.find(name);
    return it == actions_.end() ? nullptr : it->second.get();
}

std::vector<const Action*> ActionRegistry::all() const {
    std::vector<const Action*> out;
    for (const auto& [actionName, action] : actions_) out.push_back(action.get());
    return out;   // std::map keeps keys sorted, so this is sorted by name
}

ActionRegistry makeDefaultRegistry() {
    ActionRegistry registry;
    registry.add(std::make_unique<ListFilesAction>());
    registry.add(std::make_unique<MoveFilesAction>());
    return registry;
}

}  // namespace easy::actions
