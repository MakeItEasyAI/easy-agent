#include "actions/browser_actions.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <thread>

#include <nlohmann/json.hpp>

#include "actions/open_actions.h"
#include "browser/chrome.h"

namespace easy::actions {

using nlohmann::json;

namespace {

std::string param(const Params& params, const std::string& key) {
    const auto it = params.find(key);
    return it == params.end() ? "" : it->second;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Shared JS: normalise text and collect every label a form element can have
// (aria-label, <label>, aria-labelledby, placeholder, name, and the question
// title used by Google Forms).
const char* kLabelHelpers = R"JS(
const norm = s => (s || '').replace(/\s+/g, ' ').replace(/\*/g, '').trim().toLowerCase();
const labelsOf = el => {
  const out = [el.getAttribute('aria-label'), el.getAttribute('placeholder'), el.getAttribute('name')];
  if (el.id) document.querySelectorAll('label[for="' + CSS.escape(el.id) + '"]').forEach(l => out.push(l.innerText));
  const wrap = el.closest('label'); if (wrap) out.push(wrap.innerText);
  const by = el.getAttribute('aria-labelledby');
  if (by) by.split(/\s+/).forEach(id => { const e = document.getElementById(id); if (e) out.push(e.innerText); });
  const item = el.closest('[role=listitem]');
  if (item) { const h = item.querySelector('[role=heading]'); if (h) out.push(h.innerText); }
  return out.map(norm).filter(Boolean);
};
const pick = (elements, want, textsOf) => {
  let partial = null;
  for (const el of elements) {
    const texts = textsOf(el);
    if (texts.some(t => t === want)) return el;
    if (!partial && texts.some(t => t.includes(want))) partial = el;
  }
  return partial;
};
)JS";

}  // namespace

// ---------------- session ----------------

BrowserSession::BrowserSession() = default;
BrowserSession::~BrowserSession() = default;

browser::ChromeBrowser* BrowserSession::get(std::string& error) {
    if (chrome_ && !chrome_->running()) chrome_.reset();   // closed by the user / crashed: start again
    if (!chrome_) {
        auto chrome = std::make_unique<browser::ChromeBrowser>();
        if (std::string err = chrome->start(); !err.empty()) {
            error = err;
            return nullptr;
        }
        chrome_ = std::move(chrome);
    }
    return chrome_.get();
}

bool BrowserSession::started() { return chrome_ && chrome_->running(); }

void BrowserSession::close() { chrome_.reset(); }   // ~ChromeBrowser closes Chrome

// ---------------- helpers ----------------

bool isFinalAction(const std::string& text) {
    const std::string t = lower(text);
    for (const char* word : {"submit", "send", "pay", "buy", "purchase", "place order", "checkout",
                             "check out", "delete", "remove", "confirm", "sign in", "log in",
                             "login", "sign up", "register", "transfer", "donate", "subscribe"}) {
        if (t.find(word) != std::string::npos) return true;
    }
    return false;
}

std::string buildFillScript(const std::string& field, const std::string& value) {
    // json::dump() turns any text into a safe JS string literal.
    return std::string("(() => {") + kLabelHelpers +
           "const want = norm(" + json(field).dump() + ");\n"
           "const value = " + json(value).dump() + ";\n"
           R"JS(
const fields = [...document.querySelectorAll('input, textarea, [contenteditable=true]')]
  .filter(el => !['hidden','submit','button','image','reset','file','radio','checkbox'].includes((el.type || '').toLowerCase()));
const el = pick(fields, want, labelsOf);
if (!el) return {ok: false, error: 'no field labelled "' + want + '" (page: ' + location.href + ')',
                 fields: fields.map(f => labelsOf(f)[0]).filter(Boolean).slice(0, 15)};
if ((el.type || '').toLowerCase() === 'password') return {ok: false, error: 'will not type into password fields'};
el.scrollIntoView({block: 'center'});
el.focus();
if (el.isContentEditable) { el.innerText = value; }
else {
  const proto = el.tagName === 'TEXTAREA' ? HTMLTextAreaElement.prototype : HTMLInputElement.prototype;
  Object.getOwnPropertyDescriptor(proto, 'value').set.call(el, value);
}
el.dispatchEvent(new Event('input', {bubbles: true}));
el.dispatchEvent(new Event('change', {bubbles: true}));
el.blur();
const now = el.isContentEditable ? el.innerText : el.value;
return {ok: now === value, error: now === value ? '' : 'the page did not accept the text'};
})())JS";
}

std::string buildClickScript(const std::string& text) {
    return std::string("(() => {") + kLabelHelpers +
           "const want = norm(" + json(text).dump() + ");\n"
           R"JS(
const clickable = [...document.querySelectorAll(
  'button, a, input[type=button], input[type=radio], input[type=checkbox], ' +
  '[role=button], [role=radio], [role=checkbox], [role=option], [role=link], [role=tab], label')];
const textsOf = el => [norm(el.innerText), norm(el.value), norm(el.getAttribute('data-value')), ...labelsOf(el)].filter(Boolean);
const el = pick(clickable, want, textsOf);
if (!el) return {ok: false, error: 'nothing to click with text "' + want + '" (page: ' + location.href + ')'};
el.scrollIntoView({block: 'center'});
el.click();
return {ok: true, error: ''};
})())JS";
}

// ---------------- browser_open ----------------

std::string BrowserOpenAction::checkParams(const Params& params) const {
    const std::string url = param(params, "url");
    if (url.empty()) return "browser_open needs \"url\"";
    if (!isSafeUrl(url)) return "url \"" + url + "\" must be a full link starting with https://";
    return "";
}

ActionResult BrowserOpenAction::run(const Params& params) {
    if (std::string problem = checkParams(params); !problem.empty()) return {false, problem, {}};
    std::string error;
    browser::ChromeBrowser* chrome = session_->get(error);
    if (chrome == nullptr) return {false, "browser_open: " + error, {}};
    const std::string url = param(params, "url");
    if (std::string err = chrome->navigate(url); !err.empty()) return {false, "browser_open: " + err, {}};
    return {true, "opened " + url + " in the agent's browser", {}};
}

// ---------------- browser_fill ----------------

std::string BrowserFillAction::checkParams(const Params& params) const {
    if (param(params, "field").empty()) return "browser_fill needs \"field\" (the label shown on the page)";
    if (params.count("value") == 0) return "browser_fill needs \"value\"";
    const std::string field = " " + lower(param(params, "field")) + " ";
    for (const char* secret : {"password", "passcode", "one-time", "one time code", " otp ",
                               "cvv", "card number", "security code"}) {
        if (field.find(secret) != std::string::npos) {
            return "never type passwords, one-time codes or card details; leave them for the user";
        }
    }
    return "";
}

ActionResult BrowserFillAction::run(const Params& params) {
    if (std::string problem = checkParams(params); !problem.empty()) return {false, problem, {}};
    std::string error;
    browser::ChromeBrowser* chrome = session_->get(error);
    if (chrome == nullptr) return {false, "browser_fill: " + error, {}};

    const std::string field = param(params, "field");
    const json result = chrome->evaluate(buildFillScript(field, param(params, "value")), error);
    if (!error.empty()) return {false, "browser_fill: " + error, {}};
    if (!result.is_object() || !result.value("ok", false)) {
        std::string why = result.is_object() ? result.value("error", std::string("failed")) : "failed";
        if (result.is_object() && result.contains("fields") && !result["fields"].empty()) {
            why += " (fields on this page: " + result["fields"].dump() + ")";
        }
        return {false, "browser_fill: " + why, {}};
    }
    return {true, "filled \"" + field + "\"", {}};
}

// ---------------- browser_click ----------------

core::RiskLevel BrowserClickAction::riskFor(const Params& params) const {
    return isFinalAction(param(params, "text")) ? core::RiskLevel::Blocked : core::RiskLevel::Confirm;
}

std::string BrowserClickAction::checkParams(const Params& params) const {
    const std::string text = param(params, "text");
    if (text.empty()) return "browser_click needs \"text\"";
    if (isFinalAction(text)) {
        return "do not click \"" + text + "\": submitting, paying, sending or deleting is left to "
               "the user. Remove this step";
    }
    return "";
}

ActionResult BrowserClickAction::run(const Params& params) {
    if (std::string problem = checkParams(params); !problem.empty()) return {false, problem, {}};
    std::string error;
    browser::ChromeBrowser* chrome = session_->get(error);
    if (chrome == nullptr) return {false, "browser_click: " + error, {}};

    const std::string text = param(params, "text");
    const json result = chrome->evaluate(buildClickScript(text), error);
    if (!error.empty()) return {false, "browser_click: " + error, {}};
    if (!result.is_object() || !result.value("ok", false)) {
        return {false, "browser_click: " +
                           (result.is_object() ? result.value("error", std::string("failed")) : "failed"),
                {}};
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));   // let the page react
    return {true, "clicked \"" + text + "\"", {}};
}

}  // namespace easy::actions
