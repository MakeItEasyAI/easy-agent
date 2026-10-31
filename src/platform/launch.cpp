#include "platform/launch.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace easy::platform {

namespace {

#if defined(__APPLE__) || defined(__linux__)
// Runs a program with exact arguments (no shell) and waits for it to finish.
// `open` / `xdg-open` hand the work to the OS and return quickly.
LaunchResult runAndWait(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    pid_t pid = 0;
    if (posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ) != 0) {
        return {false, "could not start " + args[0]};
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return {false, "could not wait for " + args[0]};
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return {true, ""};
    return {false, args[0] + " failed (exit code " +
                       std::to_string(WIFEXITED(status) ? WEXITSTATUS(status) : -1) + ")"};
}
#endif

}  // namespace

LaunchResult openApp(const std::string& appName) {
#if defined(__APPLE__)
    LaunchResult r = runAndWait({"open", "-a", appName});
    if (!r.ok) r.error = "could not open \"" + appName + "\" (is it installed?)";
    return r;
#else
    (void)appName;
    return {false, "open_app is only supported on macOS for now"};
#endif
}

LaunchResult openUrl(const std::string& url, const std::string& appName) {
#if defined(__APPLE__)
    if (appName.empty()) return runAndWait({"open", url});
    LaunchResult r = runAndWait({"open", "-a", appName, url});
    if (!r.ok) r.error = "could not open the page in \"" + appName + "\" (is it installed?)";
    return r;
#elif defined(__linux__)
    if (!appName.empty()) return {false, "choosing a browser is only supported on macOS for now"};
    return runAndWait({"xdg-open", url});
#else
    (void)url;
    (void)appName;
    return {false, "open_url is not supported on this system yet"};
#endif
}

bool appExists(const std::string& appName) {
#if defined(__APPLE__)
    namespace fs = std::filesystem;
    const char* home = std::getenv("HOME");
    std::vector<fs::path> folders = {"/Applications", "/System/Applications",
                                     "/System/Applications/Utilities",
                                     "/Applications/Utilities"};
    if (home != nullptr) folders.emplace_back(fs::path(home) / "Applications");

    std::error_code ec;
    for (const fs::path& folder : folders) {
        if (fs::exists(folder / (appName + ".app"), ec)) return true;
    }
    // Names are case-insensitive on macOS: "google chrome" == "Google Chrome".
    std::string wanted = appName + ".app";
    std::transform(wanted.begin(), wanted.end(), wanted.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const fs::path& folder : folders) {
        for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
            std::string name = it->path().filename().string();
            std::transform(name.begin(), name.end(), name.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (name == wanted) return true;
        }
        ec.clear();
    }
    return false;
#else
    (void)appName;
    return true;
#endif
}

}  // namespace easy::platform
