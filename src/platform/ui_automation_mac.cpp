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

// Front window of the running app called `app` (case-insensitive).
Element findAppWindow(const std::string& app, Element& appElement, std::string& error) {
    const std::string want = uiNormalize(app);
    pid_t pid = 0;

    CF<CFArrayRef> windows(CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID));
    if (windows) {
        for (CFIndex i = 0; i < CFArrayGetCount(windows.get()) && pid == 0; ++i) {
            const auto info = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windows.get(), i));
            const auto owner = static_cast<CFTypeRef>(CFDictionaryGetValue(info, kCGWindowOwnerName));
            if (uiNormalize(toString(owner)) != want) continue;
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
    // Chrome and Electron apps only build their page tree for accessibility
    // tools that ask for it.
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

// Walks the window's element tree (breadth first, with limits so a huge web
// page can never hang the agent) and keeps the best label match.
Found search(AXUIElementRef window, Kind kind, const std::string& wanted) {
    Found found;
    std::set<std::string> seenSet;
    std::deque<std::pair<Element, int>> queue;
    CFRetain(window);
    queue.emplace_back(Element(window), 0);

    std::size_t visited = 0;
    while (!queue.empty() && visited < 40000) {
        auto [el, depth] = std::move(queue.front());
        queue.pop_front();
        ++visited;

        const std::string role = attributeText(el.get(), kAXRoleAttribute);
        const std::string subrole = attributeText(el.get(), kAXSubroleAttribute);
        const bool isField = isOneOf(role, {"AXTextField", "AXTextArea", "AXComboBox"});
        const bool isClickable = isOneOf(role, {"AXButton", "AXRadioButton", "AXCheckBox", "AXLink",
                                                "AXMenuItem", "AXPopUpButton", "AXMenuButton",
                                                "AXTab", "AXDisclosureTriangle"});

        if ((kind == Kind::Field && isField) || (kind == Kind::Clickable && isClickable)) {
            std::vector<std::string> labels = labelsOf(el.get());
            if (kind == Kind::Clickable) labels.push_back(attributeText(el.get(), kAXValueAttribute));
            for (const std::string& l : labels) {
                if (!uiNormalize(l).empty() && seenSet.size() < 25) seenSet.insert(uiNormalize(l));
            }
            const int score = uiMatchScore(labels, wanted);
            if (score > found.score) {
                found.score = score;
                found.secure = subrole == "AXSecureTextField";
                CFRetain(el.get());
                found.element = Element(el.get());
                if (score == 2) break;   // exact match: stop looking
            }
        }

        if (depth >= 60) continue;
        auto children = attribute(el.get(), kAXChildrenAttribute);
        if (!children || CFGetTypeID(children.get()) != CFArrayGetTypeID()) continue;
        const auto array = static_cast<CFArrayRef>(children.get());
        for (CFIndex i = 0; i < CFArrayGetCount(array); ++i) {
            auto child = static_cast<AXUIElementRef>(CFArrayGetValueAtIndex(array, i));
            CFRetain(child);
            queue.emplace_back(Element(child), depth + 1);
        }
    }
    found.seen.assign(seenSet.begin(), seenSet.end());
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

}  // namespace easy::platform
