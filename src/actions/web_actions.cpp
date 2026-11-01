#include "actions/web_actions.h"

#include <algorithm>
#include <cctype>

#include <nlohmann/json.hpp>

#include "native/browser_link.h"

namespace easy::actions {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Sends one command to the Chrome extension (front tab) and turns the reply
// into an ActionResult. Shared by web_click and web_fill.
ActionResult sendToPage(const nlohmann::json& request, const std::string& okMessage) {
    native::BrowserLink link;
    std::string error;
    if (!link.open(error)) return {false, error, {}};
    nlohmann::json result;
    if (!link.command(request, result, error)) return {false, error, {}};
    if (result.is_object() && result.value("ok", false)) return {true, okMessage, {}};
    const std::string why =
        result.is_object() ? result.value("error", std::string("the page did not accept that"))
                           : "the page did not accept that";
    return {false, why, {}};
}

}  // namespace

std::string WebClickAction::checkParams(const Params& params) const {
    const auto it = params.find("text");
    if (it == params.end() || it->second.empty()) return "web_click needs \"text\" (what to click)";
    return "";
}

ActionResult WebClickAction::run(const Params& params) {
    const auto it = params.find("text");
    if (it == params.end() || it->second.empty()) return {false, "web_click needs \"text\"", {}};
    return sendToPage({{"action", "click"}, {"text", it->second}}, "clicked \"" + it->second + "\"");
}

bool WebClickAction::verify(const Params&) { return true; }

std::string WebFillAction::checkParams(const Params& params) const {
    const auto field = params.find("field");
    if (field == params.end() || field->second.empty()) return "web_fill needs \"field\" (the label)";
    if (params.find("value") == params.end()) return "web_fill needs \"value\"";
    const std::string f = lower(field->second);
    for (const char* banned : {"password", "otp", "one-time", "cvv", "card number", "pin "}) {
        if (f.find(banned) != std::string::npos) return "web_fill will not type into " + field->second;
    }
    return "";
}

ActionResult WebFillAction::run(const Params& params) {
    const auto field = params.find("field");
    const auto value = params.find("value");
    if (field == params.end() || field->second.empty()) return {false, "web_fill needs \"field\"", {}};
    if (value == params.end()) return {false, "web_fill needs \"value\"", {}};
    return sendToPage({{"action", "fill"}, {"field", field->second}, {"value", value->second}},
                      "filled \"" + field->second + "\"");
}

bool WebFillAction::verify(const Params&) { return true; }

}  // namespace easy::actions
