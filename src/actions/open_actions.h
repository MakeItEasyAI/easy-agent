#pragma once

#include <string>
#include <vector>

#include "actions/action.h"

// open_app / open_url: start an app or open a web page.
namespace easy::actions {

// Letters, digits, spaces and . - _ only; max 64; must not start with '-' or
// a space (so it can never look like a command option).
bool isSafeAppName(const std::string& name);

// Only http:// or https://, no spaces or control characters, max 2048.
bool isSafeUrl(const std::string& url);

// open_app
//   params: app (required) e.g. "Google Chrome"
//   risk:   Safe if the app is in allowed_apps (config), otherwise Confirm
class OpenAppAction : public Action {
public:
    explicit OpenAppAction(std::vector<std::string> allowedApps = {})
        : allowedApps_(std::move(allowedApps)) {}

    std::string name() const override { return "open_app"; }
    std::string description() const override {
        return "Open an application on this computer. "
               "params: app (required, the app's name, e.g. \"Google Chrome\").";
    }
    core::RiskLevel risk() const override { return core::RiskLevel::Confirm; }
    core::RiskLevel riskFor(const Params& params) const override;
    std::vector<std::string> pathParams() const override { return {}; }
    ActionResult run(const Params& params) override;
    bool verify(const Params& params) override;

private:
    std::vector<std::string> allowedApps_;
};

// open_url
//   params: url (required, http:// or https://), app (optional browser)
//   risk:   always Confirm (a web page can do anything)
class OpenUrlAction : public Action {
public:
    std::string name() const override { return "open_url"; }
    std::string description() const override {
        return "Open a web page. params: url (required, must start with https://), "
               "app (optional, the browser to use, e.g. \"Google Chrome\").";
    }
    core::RiskLevel risk() const override { return core::RiskLevel::Confirm; }
    std::vector<std::string> pathParams() const override { return {}; }
    ActionResult run(const Params& params) override;
    bool verify(const Params& params) override;
};

}  // namespace easy::actions
