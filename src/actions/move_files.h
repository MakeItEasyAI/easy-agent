#pragma once

#include "actions/action.h"

// move_files: move files into a destination folder.
//   params: files (required)  one path, or several separated by newlines
//                             (the replay engine fills this from `$var`)
//           to    (required)  destination folder, created if missing
//   outputs: new paths of the moved files
//
// Safety:
//   - Checks every file first; if anything is wrong, moves NOTHING.
//   - Never overwrites a file that already exists in the destination.
//   - Risk level Confirm: the SafetyGuard asks the user before it runs.
namespace easy::actions {

class MoveFilesAction : public Action {
public:
    std::string name() const override { return "move_files"; }
    core::RiskLevel risk() const override { return core::RiskLevel::Confirm; }
    ActionResult run(const Params& params) override;
    bool verify(const Params& params) override;
};

}  // namespace easy::actions
