#pragma once

#include <string>
#include <vector>

#include "actions/action.h"

// ui_fill / ui_click: use any app's window like a person (keyboard + mouse
// through the OS accessibility system). This is how the agent fills forms
// in your NORMAL browser, where you are already signed in.
//
//   ui_fill   type into a field found by its label      Confirm (Safe for allowed_apps)
//   ui_click  press a button / choice by its text       Confirm; Submit/Send/Pay/Buy/
//                                                       Delete/Sign in... are Blocked
// Never: password / OTP / card fields, terminal apps (typing there can run commands).
namespace easy::actions {

// Button texts that would submit, pay, delete, sign in... Never pressed by the agent.
bool isFinalAction(const std::string& text);

// Field labels for secrets the agent must never type (password, OTP, card...).
bool isSecretField(const std::string& label);

// Terminal / shell apps: typing into them can run commands, so they are refused.
bool isTerminalApp(const std::string& app);

class UiFillAction : public Action {
public:
    explicit UiFillAction(std::vector<std::string> allowedApps = {}) : allowedApps_(std::move(allowedApps)) {}

    std::string name() const override { return "ui_fill"; }
    std::string description() const override {
        return "Type text into a field in an app's open window, like a person would; works for "
               "web forms in the user's normal browser. params: app (required, e.g. \"Google "
               "Chrome\"), field (required, the question or label shown next to the field, e.g. "
               "\"Name\"), value (required, the text to type). Never for passwords.";
    }
    core::RiskLevel risk() const override { return core::RiskLevel::Confirm; }
    core::RiskLevel riskFor(const Params& params) const override;
    std::string checkParams(const Params& params) const override;
    std::vector<std::string> pathParams() const override { return {}; }
    ActionResult run(const Params& params) override;
    bool verify(const Params&) override { return true; }   // run() reads the value back

private:
    std::vector<std::string> allowedApps_;
};

class UiClickAction : public Action {
public:
    std::string name() const override { return "ui_click"; }
    std::string description() const override {
        return "Press a button, link, or choice (radio button / checkbox) by its visible text in "
               "an app's open window. params: app (required), text (required). Never for Submit, "
               "Send, Pay, Buy or Delete: the user does that.";
    }
    core::RiskLevel risk() const override { return core::RiskLevel::Confirm; }
    core::RiskLevel riskFor(const Params& params) const override;
    std::string checkParams(const Params& params) const override;
    std::vector<std::string> pathParams() const override { return {}; }
    ActionResult run(const Params& params) override;
    bool verify(const Params&) override { return true; }
};

}  // namespace easy::actions
