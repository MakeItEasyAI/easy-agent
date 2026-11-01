#include "actions/registry.h"

#include "actions/document_actions.h"
#include "actions/list_files.h"
#include "actions/move_files.h"
#include "actions/open_actions.h"
#include "actions/ui_actions.h"

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

ActionRegistry makeDefaultRegistry(const std::vector<std::string>& allowedApps,
                                   std::shared_ptr<AiAccess> ai) {
    ActionRegistry registry;
    registry.add(std::make_unique<ListFilesAction>());
    registry.add(std::make_unique<MoveFilesAction>());
    registry.add(std::make_unique<OpenAppAction>(allowedApps));
    registry.add(std::make_unique<OpenUrlAction>());
    registry.add(std::make_unique<UiFillAction>(allowedApps));
    registry.add(std::make_unique<UiClickAction>());
    registry.add(std::make_unique<ReadPdfAction>());
    registry.add(std::make_unique<AiAnswerAction>(std::move(ai)));
    return registry;
}

}  // namespace easy::actions
