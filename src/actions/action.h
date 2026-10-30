#pragma once

#include <map>
#include <string>

#include "core/safety_guard.h"

// A single unit of work the agent can perform. Implementations (planned):
//   open_app, open_url, list_files, move_files, rename_file, click, type
namespace easy::actions {

using Params = std::map<std::string, std::string>;

struct ActionResult {
    bool ok = false;
    std::string message;
};

class Action {
public:
    virtual ~Action() = default;
    virtual std::string name() const = 0;
    virtual core::RiskLevel risk() const = 0;
    virtual ActionResult run(const Params& params) = 0;
    virtual bool verify(const Params& params) = 0;   // did it actually work?
};

}  // namespace easy::actions
