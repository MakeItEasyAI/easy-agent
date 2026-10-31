#pragma once

#include <string>

// Opening apps and web pages. The only OS-specific code these actions need.
// Programs are started directly with fixed arguments (never through a shell),
// so names/URLs can never be run as commands.
namespace easy::platform {

struct LaunchResult {
    bool ok = false;
    std::string error;
};

// "Google Chrome" -> opens the app (macOS: open -a "Google Chrome").
LaunchResult openApp(const std::string& appName);

// Opens a URL in the default browser, or in `appName` if not empty.
LaunchResult openUrl(const std::string& url, const std::string& appName);

}  // namespace easy::platform
