// Systems without UI automation support yet (Linux for now).
#include "platform/ui_automation.h"

namespace easy::platform {

bool uiPermissionOk(std::string& howTo) {
    howTo = "controlling app windows is not supported on this system yet (macOS and Windows only)";
    return false;
}

UiResult uiFill(const std::string&, const std::string&, const std::string&, int) {
    return {false, "ui_fill is not supported on this system yet (macOS and Windows only)", {}};
}

UiResult uiClick(const std::string&, const std::string&, int) {
    return {false, "ui_click is not supported on this system yet (macOS and Windows only)", {}};
}

}  // namespace easy::platform
