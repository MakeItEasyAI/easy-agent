// Windows: control app windows through UI Automation (the system Narrator
// uses). Works with normal apps and web pages in Chrome / Edge.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <UIAutomation.h>

#include <chrono>
#include <set>
#include <thread>

#include "platform/ui_automation.h"

namespace easy::platform {

namespace {

// Owns one COM pointer and releases it automatically.
template <class T>
class Com {
public:
    Com() = default;
    ~Com() { reset(); }
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    T** put() { reset(); return &ptr_; }
    T* get() const { return ptr_; }
    T* operator->() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }
    void reset() { if (ptr_) { ptr_->Release(); ptr_ = nullptr; } }

private:
    T* ptr_ = nullptr;
};

struct Bstr {
    BSTR value = nullptr;
    ~Bstr() { if (value) SysFreeString(value); }
};

struct ComScope {
    HRESULT hr;
    ComScope() : hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComScope() { if (SUCCEEDED(hr)) CoUninitialize(); }
};

std::wstring wide(const std::string& s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 1) return L"";
    std::wstring w(static_cast<std::size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

std::string narrow(const wchar_t* w) {
    if (w == nullptr) return "";
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return "";
    std::string s(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

// ---- find the app's front window (EnumWindows goes front to back) ----

struct WindowSearch {
    std::string want;   // normalised app name, e.g. "google chrome"
    HWND found = nullptr;
};

std::string exeName(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) return "";
    wchar_t path[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    std::string name;
    if (QueryFullProcessImageNameW(process, 0, path, &size)) {
        name = narrow(path);
        const auto slash = name.find_last_of("\\/");
        if (slash != std::string::npos) name = name.substr(slash + 1);
        if (name.size() > 4 && uiNormalize(name.substr(name.size() - 4)) == ".exe") name.resize(name.size() - 4);
    }
    CloseHandle(process);
    return uiNormalize(name);
}

BOOL CALLBACK onWindow(HWND hwnd, LPARAM param) {
    auto* search = reinterpret_cast<WindowSearch*>(param);
    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
    wchar_t title[512] = {};
    GetWindowTextW(hwnd, title, 512);
    const std::string t = uiNormalize(narrow(title));
    if (t.empty()) return TRUE;

    // "Google Chrome" matches a window titled "... - Google Chrome" or the
    // program chrome.exe (last word of the name).
    const std::string exe = exeName(hwnd);
    const auto space = search->want.find_last_of(' ');
    const std::string lastWord = space == std::string::npos ? search->want : search->want.substr(space + 1);
    if (t.find(search->want) != std::string::npos || exe == search->want || exe == lastWord) {
        search->found = hwnd;
        return FALSE;   // stop: front-most match
    }
    return TRUE;
}

HWND findAppWindow(const std::string& app) {
    WindowSearch search{uiNormalize(app)};
    EnumWindows(onWindow, reinterpret_cast<LPARAM>(&search));
    return search.found;
}

// ---- element labels and search ----

std::vector<std::string> labelsOf(IUIAutomationElement* el) {
    std::vector<std::string> labels;
    Bstr name, help;
    if (SUCCEEDED(el->get_CurrentName(&name.value))) labels.push_back(narrow(name.value));
    if (SUCCEEDED(el->get_CurrentHelpText(&help.value))) labels.push_back(narrow(help.value));
    Com<IUIAutomationElement> labelledBy;
    if (SUCCEEDED(el->get_CurrentLabeledBy(labelledBy.put())) && labelledBy) {
        Bstr label;
        if (SUCCEEDED(labelledBy->get_CurrentName(&label.value))) labels.push_back(narrow(label.value));
    }
    return labels;
}

enum class Kind { Field, Clickable };

struct Found {
    Com<IUIAutomationElement> element;
    int score = 0;
    bool secure = false;
    std::vector<std::string> seen;
};

bool makeTypeCondition(IUIAutomation* uia, Kind kind, IUIAutomationCondition** out) {
    const std::vector<CONTROLTYPEID> types =
        kind == Kind::Field
            ? std::vector<CONTROLTYPEID>{UIA_EditControlTypeId, UIA_ComboBoxControlTypeId}
            : std::vector<CONTROLTYPEID>{UIA_ButtonControlTypeId, UIA_RadioButtonControlTypeId,
                                         UIA_CheckBoxControlTypeId, UIA_HyperlinkControlTypeId,
                                         UIA_ListItemControlTypeId, UIA_MenuItemControlTypeId,
                                         UIA_TabItemControlTypeId, UIA_SplitButtonControlTypeId};
    Com<IUIAutomationCondition> combined;
    for (CONTROLTYPEID type : types) {
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_I4;
        v.lVal = type;
        Com<IUIAutomationCondition> one;
        if (FAILED(uia->CreatePropertyCondition(UIA_ControlTypePropertyId, v, one.put()))) return false;
        if (!combined) {
            one->AddRef();
            *combined.put() = one.get();
        } else {
            Com<IUIAutomationCondition> both;
            if (FAILED(uia->CreateOrCondition(combined.get(), one.get(), both.put()))) return false;
            both->AddRef();
            *combined.put() = both.get();
        }
    }
    combined->AddRef();
    *out = combined.get();
    return true;
}

Found search(IUIAutomation* uia, IUIAutomationElement* root, Kind kind, const std::string& wanted) {
    Found found;
    Com<IUIAutomationCondition> condition;
    if (!makeTypeCondition(uia, kind, condition.put())) return found;
    Com<IUIAutomationElementArray> all;
    if (FAILED(root->FindAll(TreeScope_Descendants, condition.get(), all.put())) || !all) return found;

    int count = 0;
    all->get_Length(&count);
    std::set<std::string> seen;
    for (int i = 0; i < count && i < 20000; ++i) {
        Com<IUIAutomationElement> el;
        if (FAILED(all->GetElement(i, el.put())) || !el) continue;
        const std::vector<std::string> labels = labelsOf(el.get());
        for (const std::string& l : labels) {
            if (!uiNormalize(l).empty() && seen.size() < 25) seen.insert(uiNormalize(l));
        }
        const int score = uiMatchScore(labels, wanted);
        if (score > found.score) {
            found.score = score;
            BOOL isPassword = FALSE;
            el->get_CurrentIsPassword(&isPassword);
            found.secure = isPassword == TRUE;
            el->AddRef();
            *found.element.put() = el.get();
            if (score == 2) break;
        }
    }
    found.seen.assign(seen.begin(), seen.end());
    return found;
}

template <class Try>
UiResult withRetries(int waitSeconds, Try attempt) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(waitSeconds);
    UiResult result;
    while (true) {
        bool retry = false;
        result = attempt(retry);
        if (result.ok || !retry || std::chrono::steady_clock::now() >= deadline) return result;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

template <class Work>
UiResult withAutomation(const std::string& app, Kind kind, const std::string& wanted, int waitSeconds,
                        Work work) {
    ComScope com;
    Com<IUIAutomation> uia;
    if (FAILED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                                __uuidof(IUIAutomation), reinterpret_cast<void**>(uia.put())))) {
        return {false, "UI Automation is not available", {}};
    }
    return withRetries(waitSeconds, [&](bool& retry) -> UiResult {
        HWND hwnd = findAppWindow(app);
        if (hwnd == nullptr) {
            retry = true;
            return {false, "\"" + app + "\" has no open window. Open it first (e.g. with open_url)", {}};
        }
        Com<IUIAutomationElement> root;
        if (FAILED(uia->ElementFromHandle(hwnd, root.put())) || !root) {
            retry = true;
            return {false, "cannot read the window of \"" + app + "\"", {}};
        }
        Found found = search(uia.get(), root.get(), kind, wanted);
        if (!found.element) {
            retry = true;
            return {false,
                    (kind == Kind::Field ? "no field labelled \"" : "nothing to click with text \"") +
                        wanted + "\" in " + app,
                    found.seen};
        }
        SetForegroundWindow(hwnd);
        return work(found);
    });
}

}  // namespace

bool uiPermissionOk(std::string& howTo) {
    howTo.clear();
    return true;   // Windows needs no permission (it cannot control apps run as Administrator)
}

UiResult uiFill(const std::string& app, const std::string& field, const std::string& value,
                int waitSeconds) {
    return withAutomation(app, Kind::Field, field, waitSeconds, [&](Found& found) -> UiResult {
        if (found.secure) return {false, "\"" + field + "\" is a password field; easy never types passwords", {}};
        Com<IUIAutomationValuePattern> pattern;
        if (FAILED(found.element->GetCurrentPatternAs(UIA_ValuePatternId, __uuidof(IUIAutomationValuePattern),
                                                      reinterpret_cast<void**>(pattern.put()))) ||
            !pattern) {
            return {false, "\"" + field + "\" in " + app + " cannot be typed into", {}};
        }
        found.element->SetFocus();
        Bstr text;
        text.value = SysAllocString(wide(value).c_str());
        if (FAILED(pattern->SetValue(text.value))) {
            return {false, "\"" + app + "\" did not accept the text for \"" + field + "\"", {}};
        }
        Bstr now;
        if (SUCCEEDED(pattern->get_CurrentValue(&now.value)) && uiNormalize(narrow(now.value)) != uiNormalize(value)) {
            return {false, "\"" + app + "\" did not accept the text for \"" + field + "\"", {}};
        }
        return {true, "", {}};
    });
}

UiResult uiClick(const std::string& app, const std::string& text, int waitSeconds) {
    return withAutomation(app, Kind::Clickable, text, waitSeconds, [&](Found& found) -> UiResult {
        Com<IUIAutomationInvokePattern> invoke;
        if (SUCCEEDED(found.element->GetCurrentPatternAs(UIA_InvokePatternId, __uuidof(IUIAutomationInvokePattern),
                                                         reinterpret_cast<void**>(invoke.put()))) &&
            invoke && SUCCEEDED(invoke->Invoke())) {
            return {true, "", {}};
        }
        Com<IUIAutomationSelectionItemPattern> select;   // radio buttons, list items
        if (SUCCEEDED(found.element->GetCurrentPatternAs(UIA_SelectionItemPatternId,
                                                         __uuidof(IUIAutomationSelectionItemPattern),
                                                         reinterpret_cast<void**>(select.put()))) &&
            select && SUCCEEDED(select->Select())) {
            return {true, "", {}};
        }
        Com<IUIAutomationTogglePattern> toggle;           // checkboxes
        if (SUCCEEDED(found.element->GetCurrentPatternAs(UIA_TogglePatternId, __uuidof(IUIAutomationTogglePattern),
                                                         reinterpret_cast<void**>(toggle.put()))) &&
            toggle && SUCCEEDED(toggle->Toggle())) {
            return {true, "", {}};
        }
        return {false, "could not press \"" + text + "\" in " + app, {}};
    });
}

std::vector<std::string> uiInspect(const std::string&, std::string& error) {
    error = "inspect is only available on macOS for now";
    return {};
}

}  // namespace easy::platform
