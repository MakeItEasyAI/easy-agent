#include "actions/web_actions.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <thread>

#include <nlohmann/json.hpp>

#include "models/model_provider.h"
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

std::string WebDoAction::checkParams(const Params& params) const {
    const auto it = params.find("goal");
    if (it == params.end() || it->second.empty()) return "web_do needs \"goal\" (what to do)";
    return "";
}

ActionResult WebDoAction::run(const Params& params) {
    const auto it = params.find("goal");
    if (it == params.end() || it->second.empty()) return {false, "web_do needs \"goal\"", {}};
    const std::string goal = it->second;

    std::string error;
    models::ModelProvider* model = ai_ ? ai_->get(error) : nullptr;
    if (model == nullptr) return {false, error.empty() ? "no AI model available for web_do" : error, {}};

    native::BrowserLink link;
    if (!link.open(error)) return {false, error, {}};

    // First, ask the model HOW to do this (it knows how common sites work), so
    // each later step follows a sensible plan instead of guessing from the page
    // cold. This stops mistakes like opening a story instead of the DM inbox.
    std::string plan;
    {
        const std::string planPrompt =
            "You will operate a web page step by step (clicking and typing) to reach a goal.\n"
            "Goal: " + goal + "\n"
            "Using your knowledge of how such websites work, list the high-level steps in order "
            "(what section to open, what to click, what to type). For example, to message someone "
            "you first open the Messages/Direct inbox, then find the person, then open their chat, "
            "then type. Do NOT include the final Submit/Send/Post/Pay/Sign in - the user does that. "
            "Keep it to a short numbered list.";
        const models::ModelReply planReply = model->askText(planPrompt);
        if (planReply.ok) plan = planReply.text;
    }

    std::vector<std::string> history;   // what we did, for the summary and the prompt
    constexpr int kMaxSteps = 6;
    for (int step = 0; step < kMaxSteps; ++step) {
        // 1) Read the current page (fields + clickable items).
        nlohmann::json page;
        if (!link.command({{"action", "read_page"}}, page, error)) return {false, error, {}};

        std::string pageText = "Fields on the page:\n";
        if (page.is_object() && page["fields"].is_array()) {
            for (const auto& f : page["fields"]) {
                if (!f.is_object()) continue;
                pageText += "- " + f.value("label", std::string());
                if (f.contains("options") && f["options"].is_array() && !f["options"].empty()) {
                    pageText += "  [";
                    bool first = true;
                    for (const auto& o : f["options"]) {
                        if (o.is_string()) { pageText += (first ? "" : " | ") + o.get<std::string>(); first = false; }
                    }
                    pageText += "]";
                }
                pageText += "\n";
            }
        }
        pageText += "Clickable items on the page:\n";
        if (page.is_object() && page["clickables"].is_array()) {
            for (const auto& c : page["clickables"]) {
                if (c.is_string()) pageText += "- " + c.get<std::string>() + "\n";
            }
        }

        std::string hist = "Steps already done:\n";
        if (history.empty()) hist += "(none yet)\n";
        for (const auto& h : history) hist += "- " + h + "\n";

        // 2) Ask the model for the single next step.
        const std::string prompt =
            "You are operating an open web page to reach a goal, ONE step at a time.\n"
            "Goal: " + goal + "\n\n"
            "Plan to follow (from your knowledge of such sites):\n" + (plan.empty() ? "(none)" : plan) +
            "\n\n" + hist + "\n" + pageText +
            "\nReply with ONLY one JSON object for the next step, nothing else:\n"
            "  {\"action\":\"fill\",\"field\":\"<a label shown above>\",\"value\":\"<text>\"}\n"
            "  {\"action\":\"click\",\"text\":\"<visible text shown above; partial is fine>\"}\n"
            "  {\"action\":\"done\"}\n"
            "Follow the plan: if it says to open a section first (e.g. Messages/Direct), click that "
            "BEFORE clicking a person's name - do NOT open stories or profiles by mistake.\n"
            "If the goal includes typing/writing a message and the page has a message or text "
            "field, you MUST use a fill action on it; do NOT reply done while the message has not "
            "been typed yet.\n"
            "Use ONLY labels/items shown above. When the goal is reached, or the only thing left "
            "is to Submit/Send/Pay/Sign in/Post, reply {\"action\":\"done\"} and the user will do "
            "that final step. Do not repeat a step already done.\n";

        const models::ModelReply reply = model->ask(prompt);
        if (!reply.ok) return {false, "AI error: " + reply.error, {}};
        const nlohmann::json act = nlohmann::json::parse(reply.text, nullptr, /*exceptions=*/false);
        if (act.is_discarded() || !act.is_object()) return {false, "the AI's step was not usable", {}};

        const std::string action = act.value("action", std::string());
        if (action == "done" || action.empty()) break;

        if (action == "fill") {
            const std::string field = act.value("field", std::string());
            const std::string value = act.value("value", std::string());
            if (field.empty()) { history.push_back("(skipped a fill with no field)"); continue; }
            nlohmann::json res;
            if (link.command({{"action", "fill"}, {"field", field}, {"value", value}}, res, error) &&
                res.is_object() && res.value("ok", false)) {
                history.push_back("typed \"" + value + "\" into " + field);
            } else {
                history.push_back("could not fill " + field);
                break;
            }
        } else if (action == "click") {
            const std::string text = act.value("text", std::string());
            if (text.empty()) { history.push_back("(skipped a click with no text)"); continue; }
            nlohmann::json res;
            if (link.command({{"action", "click"}, {"text", text}}, res, error) &&
                res.is_object() && res.value("ok", false)) {
                history.push_back("clicked \"" + text + "\"");
            } else {
                const std::string why = res.is_object() ? res.value("error", error) : error;
                history.push_back("could not click \"" + text + "\" (" + why + ")");
                break;
            }
        } else {
            break;   // unknown action
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(900));   // let the page react
    }

    std::string msg;
    if (history.empty()) {
        msg = "did nothing (the AI found no step to take for: " + goal + ")";
        return {false, msg, {}};
    }
    msg = "did: ";
    for (std::size_t i = 0; i < history.size(); ++i) msg += (i ? "; " : "") + history[i];
    msg += ". Review it in Chrome and press Send/Submit yourself if needed.";
    return {true, msg, {}};
}

bool WebDoAction::verify(const Params&) { return true; }

}  // namespace easy::actions
