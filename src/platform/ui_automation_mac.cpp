// macOS: control app windows through the Accessibility API (AXUIElement),
// the system VoiceOver uses. Works with normal apps and with web pages in
// Chrome/Safari (they expose page fields to accessibility).
#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>

#include <chrono>
#include <cstring>
#include <deque>
#include <set>
#include <thread>
#include <algorithm>

#include "platform/ui_automation.h"

namespace easy::platform {

namespace {

// Owns one CoreFoundation reference and releases it automatically.
template <class T>
class CF {
public:
    CF() = default;
    explicit CF(T ref) : ref_(ref) {}
    ~CF() { if (ref_) CFRelease(ref_); }
    CF(const CF&) = delete;
    CF& operator=(const CF&) = delete;
    CF(CF&& other) noexcept : ref_(other.ref_) { other.ref_ = nullptr; }
    CF& operator=(CF&& other) noexcept {
        if (this != &other) {
            if (ref_) CFRelease(ref_);
            ref_ = other.ref_;
            other.ref_ = nullptr;
        }
        return *this;
    }
    T get() const { return ref_; }
    explicit operator bool() const { return ref_ != nullptr; }

private:
    T ref_ = nullptr;
};

using Element = CF<AXUIElementRef>;

CF<CFStringRef> cfString(const std::string& s) {
    return CF<CFStringRef>(CFStringCreateWithCString(nullptr, s.c_str(), kCFStringEncodingUTF8));
}

std::string toString(CFTypeRef value) {
    if (value == nullptr || CFGetTypeID(value) != CFStringGetTypeID()) return "";
    const auto s = static_cast<CFStringRef>(value);
    const CFIndex max = CFStringGetMaximumSizeForEncoding(CFStringGetLength(s), kCFStringEncodingUTF8) + 1;
    std::string out(static_cast<std::size_t>(max), '\0');
    if (!CFStringGetCString(s, out.data(), max, kCFStringEncodingUTF8)) return "";
    out.resize(std::strlen(out.c_str()));
    return out;
}

CF<CFTypeRef> attribute(AXUIElementRef el, CFStringRef name) {
    CFTypeRef value = nullptr;
    if (AXUIElementCopyAttributeValue(el, name, &value) != kAXErrorSuccess) return CF<CFTypeRef>();
    return CF<CFTypeRef>(value);
}

std::string attributeText(AXUIElementRef el, CFStringRef name) {
    const auto value = attribute(el, name);
    return toString(value.get());
}

Element attributeElement(AXUIElementRef el, CFStringRef name) {
    auto value = attribute(el, name);
    if (!value || CFGetTypeID(value.get()) != AXUIElementGetTypeID()) return Element();
    CFRetain(value.get());
    return Element(static_cast<AXUIElementRef>(value.get()));
}

// Every name the element can be known by.
std::vector<std::string> labelsOf(AXUIElementRef el) {
    std::vector<std::string> labels = {
        attributeText(el, kAXTitleAttribute),
        attributeText(el, kAXDescriptionAttribute),
        attributeText(el, CFSTR("AXPlaceholderValue")),
        attributeText(el, kAXHelpAttribute),
    };
    if (Element title = attributeElement(el, kAXTitleUIElementAttribute)) {
        labels.push_back(attributeText(title.get(), kAXValueAttribute));
        labels.push_back(attributeText(title.get(), kAXTitleAttribute));
    }
    return labels;
}

bool isOneOf(const std::string& role, std::initializer_list<const char*> roles) {
    for (const char* r : roles) {
        if (role == r) return true;
    }
    return false;
}

// Text shown inside an element, e.g. <a><span>Notifications</span></a>: the
// link itself has no title, its child text does. Looks a few levels down.
void collectInnerText(AXUIElementRef el, int depth, std::vector<std::string>& out) {
    if (depth > 4 || out.size() >= 4) return;
    auto children = attribute(el, kAXChildrenAttribute);
    if (!children || CFGetTypeID(children.get()) != CFArrayGetTypeID()) return;
    const auto array = static_cast<CFArrayRef>(children.get());
    for (CFIndex i = 0; i < CFArrayGetCount(array) && i < 20 && out.size() < 4; ++i) {
        auto child = static_cast<AXUIElementRef>(CFArrayGetValueAtIndex(array, i));
        if (attributeText(child, kAXRoleAttribute) == "AXStaticText") {
            const std::string text = attributeText(child, kAXValueAttribute);
            if (!text.empty()) out.push_back(text);
        } else {
            collectInnerText(child, depth + 1, out);
        }
    }
}

// Front window of the running app called `app` (case-insensitive).
Element findAppWindow(const std::string& app, Element& appElement, std::string& error) {
    const std::string want = uiNormalize(app);
    pid_t pid = 0;

    CF<CFArrayRef> windows(CGWindowListCopyWindowInfo(
        kCGWindowListOptionAll | kCGWindowListExcludeDesktopElements, kCGNullWindowID));
    if (windows) {
        // All normal windows (layer 0), also ones on another desktop / behind a
        // full-screen Terminal. The list is front-to-back.
        for (CFIndex i = 0; i < CFArrayGetCount(windows.get()) && pid == 0; ++i) {
            const auto info = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windows.get(), i));
            const auto owner = static_cast<CFTypeRef>(CFDictionaryGetValue(info, kCGWindowOwnerName));
            if (uiNormalize(toString(owner)) != want) continue;
            const auto layerNumber = static_cast<CFNumberRef>(CFDictionaryGetValue(info, kCGWindowLayer));
            int layer = -1;
            if (!layerNumber || !CFNumberGetValue(layerNumber, kCFNumberIntType, &layer) || layer != 0) continue;
            const auto pidNumber = static_cast<CFNumberRef>(CFDictionaryGetValue(info, kCGWindowOwnerPID));
            int value = 0;
            if (pidNumber && CFNumberGetValue(pidNumber, kCFNumberIntType, &value)) pid = value;
        }
    }
    if (pid == 0) {
        error = "\"" + app + "\" has no open window. Open it first (e.g. with open_url or open_app)";
        return Element();
    }

    appElement = Element(AXUIElementCreateApplication(pid));
    AXUIElementSetMessagingTimeout(appElement.get(), 3.0f);
    // Browsers only share a web page's contents with accessibility tools that
    // ask for it: Chrome/Safari listen to AXEnhancedUserInterface, Electron
    // apps to AXManualAccessibility. The first time, the page tree is built in
    // the background, so the caller retries for a few seconds.
    AXUIElementSetAttributeValue(appElement.get(), CFSTR("AXEnhancedUserInterface"), kCFBooleanTrue);
    AXUIElementSetAttributeValue(appElement.get(), CFSTR("AXManualAccessibility"), kCFBooleanTrue);

    for (CFStringRef which : {kAXFocusedWindowAttribute, kAXMainWindowAttribute}) {
        if (Element w = attributeElement(appElement.get(), which)) return w;
    }
    auto list = attribute(appElement.get(), kAXWindowsAttribute);
    if (list && CFGetTypeID(list.get()) == CFArrayGetTypeID() &&
        CFArrayGetCount(static_cast<CFArrayRef>(list.get())) > 0) {
        auto first = static_cast<AXUIElementRef>(CFArrayGetValueAtIndex(static_cast<CFArrayRef>(list.get()), 0));
        CFRetain(first);
        return Element(first);
    }
    error = "cannot read the window of \"" + app + "\"";
    return Element();
}

enum class Kind { Field, Clickable };

struct Found {
    Element element;
    int score = 0;
    bool secure = false;                 // best match is a password field
    std::vector<std::string> seen;       // labels seen (for the error message)
};

// Walks the window's element tree breadth first, with limits so a huge web
// page can never hang the agent. visit(el, role, depth, inWebPage) returns
// false to stop. Returns how many elements were visited.
template <class Visit>
std::size_t walk(AXUIElementRef window, Visit visit) {
    struct Node {
        Element el;
        int depth;
        bool inWeb;
    };
    std::deque<Node> queue;
    CFRetain(window);
    queue.push_back({Element(window), 0, false});

    std::size_t visited = 0;
    while (!queue.empty() && visited < 60000) {
        Node node = std::move(queue.front());
        queue.pop_front();
        ++visited;

        const std::string role = attributeText(node.el.get(), kAXRoleAttribute);
        const bool inWeb = node.inWeb || role == "AXWebArea";
        if (!visit(node.el.get(), role, node.depth, inWeb)) break;

        if (node.depth >= 80) continue;
        auto children = attribute(node.el.get(), kAXChildrenAttribute);
        if (!children || CFGetTypeID(children.get()) != CFArrayGetTypeID()) continue;
        const auto array = static_cast<CFArrayRef>(children.get());
        for (CFIndex i = 0; i < CFArrayGetCount(array); ++i) {
            auto child = static_cast<AXUIElementRef>(CFArrayGetValueAtIndex(array, i));
            CFRetain(child);
            queue.push_back({Element(child), node.depth + 1, inWeb});
        }
    }
    return visited;
}

bool isFieldRole(const std::string& role) { return isOneOf(role, {"AXTextField", "AXTextArea", "AXComboBox"}); }

bool isClickableRole(const std::string& role) {
    return isOneOf(role, {"AXButton", "AXRadioButton", "AXCheckBox", "AXLink", "AXMenuItem",
                          "AXPopUpButton", "AXMenuButton", "AXTab", "AXDisclosureTriangle"});
}

std::vector<std::string> labelsFor(AXUIElementRef el, bool clickable) {
    std::vector<std::string> labels = labelsOf(el);
    if (clickable) {
        labels.push_back(attributeText(el, kAXValueAttribute));
        collectInnerText(el, 0, labels);   // text inside links / buttons
    }
    return labels;
}

// Finds the element whose labels best match `wanted`.
Found search(AXUIElementRef window, Kind kind, const std::string& wanted) {
    Found found;
    std::set<std::string> webSeen, otherSeen;   // page labels are the useful ones to show

    walk(window, [&](AXUIElementRef el, const std::string& role, int, bool inWeb) {
        const bool wantedKind = kind == Kind::Field ? isFieldRole(role) : isClickableRole(role);
        if (!wantedKind) return true;

        const std::vector<std::string> labels = labelsFor(el, kind == Kind::Clickable);
        for (const std::string& l : labels) {
            const std::string n = uiNormalize(l);
            if (n.empty()) continue;
            std::set<std::string>& into = inWeb ? webSeen : otherSeen;
            if (into.size() < 25) into.insert(n);
        }
        const int score = uiMatchScore(labels, wanted);
        if (score > found.score) {
            found.score = score;
            found.secure = attributeText(el, kAXSubroleAttribute) == "AXSecureTextField";
            CFRetain(el);
            found.element = Element(el);
            if (score == 2) return false;   // exact match: stop looking
        }
        return true;
    });

    found.seen.assign(webSeen.begin(), webSeen.end());
    if (found.seen.size() < 25) {
        for (const std::string& s : otherSeen) {
            if (found.seen.size() >= 25) break;
            found.seen.push_back(s);
        }
    }
    return found;
}

// Types text into whatever has keyboard focus (fallback when an app does not
// accept a direct value change). Newlines are not typed: they could press
// Enter and submit or run something.
void typeText(const std::string& text) {
    std::string safe;
    for (char c : text) safe += (c == '\n' || c == '\r') ? ' ' : c;

    CF<CFStringRef> s = cfString(safe);
    const CFIndex length = CFStringGetLength(s.get());
    std::vector<UniChar> chars(static_cast<std::size_t>(length));
    CFStringGetCharacters(s.get(), CFRangeMake(0, length), chars.data());

    // Select what is already in the field (Cmd+A), so typing replaces it.
    CF<CGEventRef> selDown(CGEventCreateKeyboardEvent(nullptr, 0 /* 'a' */, true));
    CF<CGEventRef> selUp(CGEventCreateKeyboardEvent(nullptr, 0, false));
    CGEventSetFlags(selDown.get(), kCGEventFlagMaskCommand);
    CGEventSetFlags(selUp.get(), kCGEventFlagMaskCommand);
    CGEventPost(kCGHIDEventTap, selDown.get());
    CGEventPost(kCGHIDEventTap, selUp.get());

    for (std::size_t i = 0; i < chars.size(); i += 16) {
        const auto count = static_cast<UniCharCount>(std::min<std::size_t>(16, chars.size() - i));
        CF<CGEventRef> down(CGEventCreateKeyboardEvent(nullptr, 0, true));
        CF<CGEventRef> up(CGEventCreateKeyboardEvent(nullptr, 0, false));
        CGEventKeyboardSetUnicodeString(down.get(), count, chars.data() + i);
        CGEventKeyboardSetUnicodeString(up.get(), count, chars.data() + i);
        CGEventPost(kCGHIDEventTap, down.get());
        CGEventPost(kCGHIDEventTap, up.get());
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
}

template <class Try>
UiResult withRetries(int waitSeconds, Try attempt) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(waitSeconds);
    UiResult result;
    while (true) {
        bool retry = false;
        result = attempt(retry);
        if (result.ok || !retry || std::chrono::steady_clock::now() >= deadline) return result;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));   // page may still be loading
    }
}

}  // namespace

bool uiPermissionOk(std::string& howTo) {
    const void* keys[] = {kAXTrustedCheckOptionPrompt};
    const void* values[] = {kCFBooleanTrue};
    CF<CFDictionaryRef> options(CFDictionaryCreate(nullptr, keys, values, 1,
                                                   &kCFCopyStringDictionaryKeyCallBacks,
                                                   &kCFTypeDictionaryValueCallBacks));
    if (AXIsProcessTrustedWithOptions(options.get())) return true;
    howTo = "macOS needs your permission to let easy control app windows.\n"
            "Open System Settings > Privacy & Security > Accessibility, turn ON the app you run\n"
            "easy from (e.g. Terminal), then run the command again.\n"
            "Note: this lets any program run from that app control your apps; turn it off when "
            "you are done if you like.";
    return false;
}

UiResult uiFill(const std::string& app, const std::string& field, const std::string& value,
                int waitSeconds) {
    std::string howTo;
    if (!uiPermissionOk(howTo)) return {false, howTo, {}};

    return withRetries(waitSeconds, [&](bool& retry) -> UiResult {
        Element appElement;
        std::string error;
        Element window = findAppWindow(app, appElement, error);
        if (!window) {
            retry = true;
            return {false, error, {}};
        }
        Found found = search(window.get(), Kind::Field, field);
        if (!found.element) {
            retry = true;
            return {false, "no field labelled \"" + field + "\" in " + app, found.seen};
        }
        if (found.secure) return {false, "\"" + field + "\" is a password field; easy never types passwords", {}};

        AXUIElementSetAttributeValue(appElement.get(), kAXFrontmostAttribute, kCFBooleanTrue);
        AXUIElementSetAttributeValue(found.element.get(), kAXFocusedAttribute, kCFBooleanTrue);

        // 1st choice: set the value directly (fast, exact). 2nd: type it.
        CF<CFStringRef> text = cfString(value);
        AXUIElementSetAttributeValue(found.element.get(), kAXValueAttribute, text.get());
        if (attributeText(found.element.get(), kAXValueAttribute) == value) return {true, "", {}};

        typeText(value);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (uiNormalize(attributeText(found.element.get(), kAXValueAttribute)) == uiNormalize(value)) {
            return {true, "", {}};
        }
        return {false, "\"" + app + "\" did not accept the text for \"" + field + "\"", {}};
    });
}

UiResult uiClick(const std::string& app, const std::string& text, int waitSeconds) {
    std::string howTo;
    if (!uiPermissionOk(howTo)) return {false, howTo, {}};

    return withRetries(waitSeconds, [&](bool& retry) -> UiResult {
        Element appElement;
        std::string error;
        Element window = findAppWindow(app, appElement, error);
        if (!window) {
            retry = true;
            return {false, error, {}};
        }
        Found found = search(window.get(), Kind::Clickable, text);
        if (!found.element) {
            retry = true;
            return {false, "nothing to click with text \"" + text + "\" in " + app, found.seen};
        }
        AXUIElementSetAttributeValue(appElement.get(), kAXFrontmostAttribute, kCFBooleanTrue);
        if (AXUIElementPerformAction(found.element.get(), kAXPressAction) != kAXErrorSuccess) {
            return {false, "could not press \"" + text + "\" in " + app, {}};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(400));   // let the app react
        return {true, "", {}};
    });
}

std::vector<std::string> uiInspect(const std::string& app, std::string& error) {
    std::string howTo;
    if (!uiPermissionOk(howTo)) {
        error = howTo;
        return {};
    }
    std::vector<std::string> lines;
    for (int pass = 1; pass <= 2; ++pass) {
        Element appElement;
        Element window = findAppWindow(app, appElement, error);
        if (!window) return {};
        if (pass == 1) {   // first look switches the page tree on; give it time to build
            std::this_thread::sleep_for(std::chrono::seconds(4));
            continue;
        }

        std::size_t webAreas = 0, webElements = 0, fields = 0, clickables = 0;
        std::vector<std::string> webLabels;
        std::set<std::string> unique;
        const std::size_t visited = walk(window.get(), [&](AXUIElementRef el, const std::string& role, int,
                                                           bool inWeb) {
            if (role == "AXWebArea") ++webAreas;
            if (inWeb) ++webElements;
            const bool field = isFieldRole(role), click = isClickableRole(role);
            if (field) ++fields;
            if (click) ++clickables;
            if (inWeb && (field || click) && webLabels.size() < 40) {
                for (const std::string& l : labelsFor(el, click)) {
                    const std::string n = uiNormalize(l);
                    if (!n.empty() && unique.insert(n).second) {
                        webLabels.push_back(std::string(field ? "field: " : "button/link: ") + n);
                        break;
                    }
                }
            }
            return true;
        });

        lines.push_back("window title: " + attributeText(window.get(), kAXTitleAttribute));
        lines.push_back("elements visited: " + std::to_string(visited));
        lines.push_back("web page areas: " + std::to_string(webAreas) + "   elements inside pages: " +
                        std::to_string(webElements));
        lines.push_back("fields: " + std::to_string(fields) + "   buttons/links: " + std::to_string(clickables));
        if (webAreas == 0) {
            lines.push_back("NO web page found: the app is not sharing page contents with accessibility.");
        }
        for (const std::string& l : webLabels) lines.push_back("  " + l);
    }
    return lines;
}

}  // namespace easy::platform
