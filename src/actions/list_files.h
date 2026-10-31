#pragma once

#include "actions/action.h"

// list_files: list files in one folder that match a pattern.
//   params: folder  (required)  e.g. "~/Downloads"
//           pattern (optional)  e.g. "*.pdf"   default "*"
//   outputs: full paths of matching files, sorted
// Read-only, so the risk level is Safe. Allowed-folder checks are done by the
// SafetyGuard before this action runs, not here.
namespace easy::actions {

class ListFilesAction : public Action {
public:
    std::string name() const override { return "list_files"; }
    core::RiskLevel risk() const override { return core::RiskLevel::Safe; }
    std::vector<std::string> pathParams() const override { return {"folder"}; }
    ActionResult run(const Params& params) override;
    bool verify(const Params& params) override;
};

}  // namespace easy::actions
