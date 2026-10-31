#include "actions/open_actions.h"

#include <algorithm>
#include <cctype>

#include "platform/launch.h"

namespace easy::actions {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string param(const Params& params, const std::string& key) {
    const auto it = params.find(key);
    return it == params.end() ? "" : it->second;
}

}  // namespace

bool isSafeAppName(const std::string& name) {
    if (name.empty() || name.size() > 64 || name[0] == '-' || name[0] == ' ') return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::isalnum(c) || c == ' ' || c == '.' || c == '-' || c == '_';
    });
}

bool isSafeUrl(const std::string& url) {
    if (url.size() > 2048) return false;
    const std::string start = lower(url.substr(0, 8));
    if (start.rfind("https://", 0) != 0 && start.rfind("http://", 0) != 0) return false;
    const std::size_t hostStart = url.find("://") + 3;
    if (hostStart >= url.size()) return false;   // "https://" with no address
    return std::none_of(url.begin(), url.end(), [](unsigned char c) {
        return std::isspace(c) || std::iscntrl(c);
    });
}

// ---- open_app ----

core::RiskLevel OpenAppAction::riskFor(const Params& params) const {
    const std::string app = lower(param(params, "app"));
    for (const std::string& allowed : allowedApps_) {
        if (lower(allowed) == app) return core::RiskLevel::Safe;
    }
    return core::RiskLevel::Confirm;
}

std::string OpenAppAction::checkParams(const Params& params) const {
    const std::string app = param(params, "app");
    if (app.empty()) return "open_app needs \"app\"";
    if (!isSafeAppName(app)) return "\"" + app + "\" is not a valid app name";
    if (!platform::appExists(app)) {
        return "no app named \"" + app +
               "\" is installed. If it is a website, use open_url with an https:// link instead";
    }
    return "";
}

std::string OpenUrlAction::checkParams(const Params& params) const {
    const std::string url = param(params, "url");
    const std::string app = param(params, "app");
    if (url.empty()) return "open_url needs \"url\"";
    if (!isSafeUrl(url)) return "url \"" + url + "\" must be a full link starting with https://";
    if (!app.empty() && !isSafeAppName(app)) return "\"" + app + "\" is not a valid app name";
    if (!app.empty() && !platform::appExists(app)) {
        return "no browser app named \"" + app + "\" is installed";
    }
    return "";
}

ActionResult OpenAppAction::run(const Params& params) {
    const std::string app = param(params, "app");
    if (app.empty()) return {false, "open_app: missing \"app\"", {}};
    if (!isSafeAppName(app)) return {false, "open_app: not a valid app name: " + app, {}};

    const platform::LaunchResult r = platform::openApp(app);
    if (!r.ok) return {false, "open_app: " + r.error, {}};
    return {true, "opened " + app, {}};
}

bool OpenAppAction::verify(const Params& /*params*/) {
    return true;   // run() already checked that the OS opened it (exit code 0)
}

// ---- open_url ----

ActionResult OpenUrlAction::run(const Params& params) {
    const std::string url = param(params, "url");
    const std::string app = param(params, "app");
    if (url.empty()) return {false, "open_url: missing \"url\"", {}};
    if (!isSafeUrl(url)) {
        return {false, "open_url: only http:// and https:// links are allowed: " + url, {}};
    }
    if (!app.empty() && !isSafeAppName(app)) {
        return {false, "open_url: not a valid app name: " + app, {}};
    }

    const platform::LaunchResult r = platform::openUrl(url, app);
    if (!r.ok) return {false, "open_url: " + r.error, {}};
    return {true, "opened " + url + (app.empty() ? "" : " in " + app), {}};
}

bool OpenUrlAction::verify(const Params& /*params*/) {
    return true;   // run() already checked that the OS opened it (exit code 0)
}

}  // namespace easy::actions
