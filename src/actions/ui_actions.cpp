#include "actions/ui_actions.h"

#include "actions/open_actions.h"
#include "platform/launch.h"
#include "platform/ui_automation.h"

namespace easy::actions {

namespace {

std::string param(const Params& params, const std::string& key) {
    const auto it = params.find(key);
    return it == params.end() ? "" : it->second;
}

bool containsAny(const std::string& text, std::initializer_list<const char*> words) {
    const std::string t = " " + platform::uiNormalize(text) + " ";
    for (const char* w : words) {
        if (t.find(w) != std::string::npos) return true;
    }
    return false;
}

std::string notFound(const platform::UiResult& r) {
    std::string out = r.error;
    if (!r.seen.empty()) {
        out += " (labels seen:";
        for (std::size_t i = 0; i < r.seen.size() && i < 15; ++i) out += (i ? ", \"" : " \"") + r.seen[i] + "\"";
        out += ")";
    }
    return out;
}

// Checks shared by both actions.
std::string checkApp(const std::string& action, const std::string& app) {
    if (app.empty()) return action + " needs \"app\" (the app whose window to use, e.g. \"Google Chrome\")";
    if (!isSafeAppName(app)) return "\"" + app + "\" is not a valid app name";
    if (isTerminalApp(app)) return "never type or click in terminal apps (" + app + "): it could run commands";
    if (!platform::appExists(app)) return "no app named \"" + app + "\" is installed";
    return "";
}

}  // namespace

bool isFinalAction(const std::string& text) {
    return containsAny(text, {"submit", "send", "pay ", "pay now", " buy", "purchase", "place order",
                              "checkout", "check out", "delete", "remove", "confirm", "sign in",
                              "log in", "login", "sign up", "register", "transfer", "donate",
                              "subscribe", "apply now", " apply "});
}

bool isSecretField(const std::string& label) {
    return containsAny(label, {"password", "passcode", "one-time", "one time code", " otp ", "cvv",
                               "card number", "security code"});
}

bool isTerminalApp(const std::string& app) {
    const std::string a = platform::uiNormalize(app);
    for (const char* t : {"terminal", "iterm", "iterm2", "warp", "alacritty", "kitty", "wezterm",
                          "hyper", "windows terminal", "powershell", "command prompt", "cmd",
                          "windows powershell", "console"}) {
        if (a == t) return true;
    }
    return false;
}

// ---------------- ui_fill ----------------

core::RiskLevel UiFillAction::riskFor(const Params& params) const {
    const std::string app = platform::uiNormalize(param(params, "app"));
    for (const std::string& allowed : allowedApps_) {
        if (platform::uiNormalize(allowed) == app) return core::RiskLevel::Safe;   // typing only, never submits
    }
    return core::RiskLevel::Confirm;
}

std::string UiFillAction::checkParams(const Params& params) const {
    if (std::string p = checkApp("ui_fill", param(params, "app")); !p.empty()) return p;
    if (param(params, "field").empty()) return "ui_fill needs \"field\" (the label shown next to the field)";
    if (params.count("value") == 0) return "ui_fill needs \"value\"";
    if (isSecretField(param(params, "field"))) {
        return "never type passwords, one-time codes or card details; leave them for the user";
    }
    return "";
}

ActionResult UiFillAction::run(const Params& params) {
    if (std::string problem = checkParams(params); !problem.empty()) return {false, "ui_fill: " + problem, {}};
    const std::string app = param(params, "app");
    const std::string field = param(params, "field");
    const platform::UiResult r = platform::uiFill(app, field, param(params, "value"));
    if (!r.ok) return {false, "ui_fill: " + notFound(r), {}};
    return {true, "filled \"" + field + "\" in " + app, {}};
}

// ---------------- ui_click ----------------

core::RiskLevel UiClickAction::riskFor(const Params& params) const {
    return isFinalAction(param(params, "text")) ? core::RiskLevel::Blocked : core::RiskLevel::Confirm;
}

std::string UiClickAction::checkParams(const Params& params) const {
    if (std::string p = checkApp("ui_click", param(params, "app")); !p.empty()) return p;
    const std::string text = param(params, "text");
    if (text.empty()) return "ui_click needs \"text\" (the button or choice text)";
    if (isFinalAction(text)) {
        return "do not press \"" + text + "\": submitting, paying, sending or deleting is left to "
               "the user. Remove this step";
    }
    return "";
}

ActionResult UiClickAction::run(const Params& params) {
    if (std::string problem = checkParams(params); !problem.empty()) return {false, "ui_click: " + problem, {}};
    const std::string app = param(params, "app");
    const std::string text = param(params, "text");
    const platform::UiResult r = platform::uiClick(app, text);
    if (!r.ok) return {false, "ui_click: " + notFound(r), {}};
    return {true, "pressed \"" + text + "\" in " + app, {}};
}

}  // namespace easy::actions
