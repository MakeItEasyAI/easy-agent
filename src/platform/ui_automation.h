#pragma once

#include <string>
#include <vector>

// Reading and using other apps' windows like a person would: find a field or
// button by its label, type into it, press it. Uses the OS accessibility
// system (the same one screen readers use):
//   macOS:   Accessibility API (AXUIElement). Needs permission in
//            System Settings > Privacy & Security > Accessibility.
//   Windows: UI Automation.
//   Linux:   not supported yet.
//
// Safety: password / secure fields are never typed into (checked here, at the
// lowest level, in addition to the action checks).
namespace easy::platform {

struct UiResult {
    bool ok = false;
    std::string error;
    std::vector<std::string> seen;   // on "not found": labels that were seen, to help fix it
};

// Is this program allowed to control other apps? If not, `howTo` explains
// how to allow it (macOS also shows its permission prompt once).
bool uiPermissionOk(std::string& howTo);

// Type `value` into the field labelled `field` in `app`'s front window.
// Retries for up to `waitSeconds` (e.g. while a web page loads).
UiResult uiFill(const std::string& app, const std::string& field, const std::string& value,
                int waitSeconds = 15);

// Press the button / link / choice whose text is `text` in `app`'s front window.
UiResult uiClick(const std::string& app, const std::string& text, int waitSeconds = 15);

// Diagnostic: what the agent can see in `app`'s front window (counts, whether
// a web page area exists, and labels inside it). For `easy inspect "<app>"`.
std::vector<std::string> uiInspect(const std::string& app, std::string& error);

// ---- shared text helpers (no OS calls; unit-tested) ----

// Lowercase, single spaces, no '*' marks (required-field stars), trimmed.
std::string uiNormalize(const std::string& text);

// How well an element's labels match what we look for:
//   2 = a label equals it, 1 = a label contains it (or it contains a label of
//   3+ letters), 0 = no match.
int uiMatchScore(const std::vector<std::string>& labels, const std::string& wanted);

}  // namespace easy::platform
