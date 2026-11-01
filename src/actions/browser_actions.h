#pragma once

#include <memory>
#include <string>

#include "actions/action.h"

namespace easy::browser {
class ChromeBrowser;
}

// browser_open / browser_fill / browser_click: control the agent's own Chrome.
//   browser_open   open a page                          Confirm
//   browser_fill   type into a field found by its label   Safe (only types; never passwords)
//   browser_click  click a button/option by its text      Confirm; Submit/Send/Pay/Buy/
//                                                         Delete/... are Blocked: you submit
namespace easy::actions {

// One agent browser shared by all browser_ steps of a run. Started on first
// use; the caller closes it when the run is over.
class BrowserSession {
public:
    BrowserSession();
    ~BrowserSession();
    browser::ChromeBrowser* get(std::string& error);   // starts Chrome if needed
    bool started();                                    // true while the agent's Chrome is open
    void close();

private:
    std::unique_ptr<browser::ChromeBrowser> chrome_;
};

// True for button texts that would submit, pay, delete, sign in... These are
// never clicked by the agent: the user does it after reviewing.
bool isFinalAction(const std::string& text);

// Exposed for tests: the JavaScript sent to the page. Text is JSON-escaped.
std::string buildFillScript(const std::string& field, const std::string& value);
std::string buildClickScript(const std::string& text);

class BrowserOpenAction : public Action {
public:
    explicit BrowserOpenAction(std::shared_ptr<BrowserSession> session) : session_(std::move(session)) {}
    std::string name() const override { return "browser_open"; }
    std::string description() const override {
        return "Open a web page in the agent's own Chrome window so later browser_fill and "
               "browser_click steps can work on it (e.g. to fill a form). "
               "params: url (required, must start with https://).";
    }
    core::RiskLevel risk() const override { return core::RiskLevel::Confirm; }
    std::string checkParams(const Params& params) const override;
    std::vector<std::string> pathParams() const override { return {}; }
    ActionResult run(const Params& params) override;
    bool verify(const Params&) override { return true; }

private:
    std::shared_ptr<BrowserSession> session_;
};

class BrowserFillAction : public Action {
public:
    explicit BrowserFillAction(std::shared_ptr<BrowserSession> session) : session_(std::move(session)) {}
    std::string name() const override { return "browser_fill"; }
    std::string description() const override {
        return "Type text into a field on the page opened by browser_open. "
               "params: field (required, the question or label shown next to the field, "
               "e.g. \"Name\"), value (required, the text to type). Never for passwords.";
    }
    core::RiskLevel risk() const override { return core::RiskLevel::Safe; }
    std::string checkParams(const Params& params) const override;
    std::string needsEarlier() const override { return "browser_open"; }
    std::vector<std::string> pathParams() const override { return {}; }
    ActionResult run(const Params& params) override;
    bool verify(const Params&) override { return true; }   // run() reads the value back

private:
    std::shared_ptr<BrowserSession> session_;
};

class BrowserClickAction : public Action {
public:
    explicit BrowserClickAction(std::shared_ptr<BrowserSession> session) : session_(std::move(session)) {}
    std::string name() const override { return "browser_click"; }
    std::string description() const override {
        return "Click a button, link, or choice (radio button / checkbox) by its visible text "
               "on the page opened by browser_open. params: text (required). "
               "Never use it for Submit, Send, Pay, Buy or Delete: the user does that.";
    }
    core::RiskLevel risk() const override { return core::RiskLevel::Confirm; }
    core::RiskLevel riskFor(const Params& params) const override;
    std::string checkParams(const Params& params) const override;
    std::string needsEarlier() const override { return "browser_open"; }
    std::vector<std::string> pathParams() const override { return {}; }
    ActionResult run(const Params& params) override;
    bool verify(const Params&) override { return true; }

private:
    std::shared_ptr<BrowserSession> session_;
};

}  // namespace easy::actions
