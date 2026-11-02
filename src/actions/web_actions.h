#pragma once

#include <memory>
#include <string>
#include <vector>

#include "actions/action.h"
#include "actions/document_actions.h"   // AiAccess

// Actions that act on the web page already open in the front Google Chrome tab,
// through the easy-agent Chrome extension. General-purpose: the AI can use them
// for any "do X on this page" task (navigate a site, click a link/tab/menu,
// fill a field), not just forms.
namespace easy::actions {

// web_click: click a link / button / tab / menu item by its visible text on the
// page open in the front Chrome tab. The extension refuses submit/pay/sign-in.
class WebClickAction : public Action {
public:
    std::string name() const override { return "web_click"; }
    std::string description() const override {
        return "Click a link, button, tab or menu item by its visible text on the web page "
               "ALREADY open in Google Chrome (uses the browser extension). Use this to move "
               "around a page you already opened, e.g. click \"Notifications\" or \"Jobs\". "
               "params: text (required, the visible text/label to click). "
               "Never clicks Submit/Send/Pay/Sign in.";
    }
    core::RiskLevel risk() const override { return core::RiskLevel::Confirm; }
    std::string checkParams(const Params& params) const override;
    std::vector<std::string> pathParams() const override { return {}; }
    ActionResult run(const Params& params) override;
    bool verify(const Params& params) override;
};

// web_fill: type a value into a field by its visible label on the page open in
// the front Chrome tab. Never types into passwords/OTP/card fields.
class WebFillAction : public Action {
public:
    std::string name() const override { return "web_fill"; }
    std::string description() const override {
        return "Type a value into a field by its visible label on the web page ALREADY open in "
               "Google Chrome (uses the browser extension). params: field (required, the label), "
               "value (required, exactly what to type). Use only values the user gave; never "
               "invent answers. Will not type into password/OTP/card fields.";
    }
    core::RiskLevel risk() const override { return core::RiskLevel::Safe; }
    std::string checkParams(const Params& params) const override;
    std::vector<std::string> pathParams() const override { return {}; }
    ActionResult run(const Params& params) override;
    bool verify(const Params& params) override;
};

// web_do: reach a plain-language goal on the open page by deciding each step
// itself (read page -> ask the AI for the next click/fill -> do it -> repeat),
// stopping before Submit/Send/Pay/Sign in. A tiny agent-loop in one step, for
// dynamic tasks like "search and play <song>" that a fixed recipe can't capture.
class WebDoAction : public Action {
public:
    explicit WebDoAction(std::shared_ptr<AiAccess> ai) : ai_(std::move(ai)) {}
    std::string name() const override { return "web_do"; }
    std::string description() const override {
        return "Reach a goal on the web page ALREADY open in Google Chrome by working out the "
               "clicks and typing itself, step by step (uses the browser extension + AI). Use for "
               "dynamic tasks, e.g. \"search for and play <song>\" or \"open the chat with <name> "
               "and type <message>\". params: goal (required, plain words). It decides each step "
               "from the live page and stops before Submit/Send/Pay/Sign in (the user does those).";
    }
    core::RiskLevel risk() const override { return core::RiskLevel::Confirm; }
    std::string checkParams(const Params& params) const override;
    std::vector<std::string> pathParams() const override { return {}; }
    ActionResult run(const Params& params) override;
    bool verify(const Params& params) override;

private:
    std::shared_ptr<AiAccess> ai_;
};

}  // namespace easy::actions
