#include "browser/chrome.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include "actions/path_utils.h"

#if defined(__APPLE__) || defined(__linux__)
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#define EASY_HAVE_SPAWN 1
#endif

namespace easy::browser {

namespace fs = std::filesystem;
using nlohmann::json;

std::string agentRunsDir() { return actions::expandHome("~/.makeiteasy/chrome-runs"); }
std::string chromeLogPath() { return actions::expandHome("~/.makeiteasy/chrome.log"); }

std::string findChrome() {
    std::error_code ec;
#if defined(__APPLE__)
    const std::string inApp = "Google Chrome.app/Contents/MacOS/Google Chrome";
    for (const std::string& base : {std::string("/Applications/"), actions::expandHome("~/Applications/")}) {
        if (fs::exists(base + inApp, ec)) return base + inApp;
    }
#elif defined(__linux__)
    const char* path = std::getenv("PATH");
    std::string dirs = path == nullptr ? "/usr/bin:/usr/local/bin" : path;
    for (const char* name : {"google-chrome", "google-chrome-stable", "chromium", "chromium-browser"}) {
        std::size_t start = 0;
        while (start <= dirs.size()) {
            const std::size_t end = dirs.find(':', start);
            const std::string dir = dirs.substr(start, end == std::string::npos ? std::string::npos : end - start);
            const fs::path candidate = fs::path(dir) / name;
            if (!dir.empty() && ::access(candidate.c_str(), X_OK) == 0) return candidate.string();
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }
#endif
    return "";
}

#if defined(EASY_HAVE_SPAWN)

std::string ChromeBrowser::start() {
    if (running()) return "";
    const std::string chrome = findChrome();
    if (chrome.empty()) return "Google Chrome is not installed";

    // A fresh profile folder for this run only (private to you: 0700).
    std::error_code ec;
    const fs::path runs = agentRunsDir();
    fs::create_directories(runs, ec);
    if (ec) return "cannot create " + runs.string();
    const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
    profile_ = (runs / ("run-" + std::to_string(::getpid()) + "-" + std::to_string(stamp))).string();
    fs::create_directory(profile_, ec);
    if (ec) return "cannot create " + profile_;
    fs::permissions(profile_, fs::perms::owner_all, fs::perm_options::replace, ec);
    const std::string profile = profile_;
    const fs::path portFile = fs::path(profile) / "DevToolsActivePort";

    std::vector<std::string> args = {
        chrome,
        "--remote-debugging-port=0",           // Chrome picks a free port (127.0.0.1)
        "--user-data-dir=" + profile,          // the agent's own profile
        "--no-first-run",
        "--no-default-browser-check",
    };
    if (const char* headless = std::getenv("EASY_BROWSER_HEADLESS"); headless && std::string(headless) == "1") {
        args.push_back("--headless=new");      // used by tests: no window
    }
    args.push_back("about:blank");

    std::vector<char*> argv;
    for (std::string& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);

    // Chrome prints a lot of internal logs: send them to a file, not the terminal.
    const std::string logPath = chromeLogPath();
    posix_spawn_file_actions_t files;
    posix_spawn_file_actions_init(&files);
    posix_spawn_file_actions_addopen(&files, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&files, STDOUT_FILENO, logPath.c_str(),
                                     O_WRONLY | O_CREAT | O_TRUNC, 0600);
    posix_spawn_file_actions_adddup2(&files, STDOUT_FILENO, STDERR_FILENO);

    pid_t pid = 0;
    const int spawned = posix_spawn(&pid, chrome.c_str(), &files, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&files);
    if (spawned != 0) {
        removeProfile();
        return "could not start Chrome";
    }
    pid_ = pid;

    // Chrome writes "<port>\n<browser path>" once remote control is ready.
    std::string browserPath;
    for (int i = 0; i < 200 && port_ == 0; ++i) {   // up to ~20 s
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        int status = 0;
        if (waitpid(pid, &status, WNOHANG) == pid) {   // Chrome quit right away
            pid_ = -1;
            removeProfile();
            return "Chrome closed right after starting (details in " + logPath + ")";
        }
        std::ifstream in(portFile);
        int port = 0;
        if (in >> port >> browserPath && port > 0 && !browserPath.empty()) port_ = port;
    }
    if (port_ == 0) {
        close();
        return "Chrome did not start remote control in time (details in " + logPath + ")";
    }

    if (std::string err = cdp_.connect(port_, browserPath); !err.empty()) {
        close();
        return "cannot control Chrome: " + err;
    }

    // Use the first normal page tab and attach to it.
    std::string err;
    const json targets = cdp_.call("Target.getTargets", json::object(), "", err);
    std::string targetId;
    if (err.empty() && targets.is_object() && targets.contains("targetInfos") &&
        targets["targetInfos"].is_array()) {
        for (const json& t : targets["targetInfos"]) {
            if (t.is_object() && t.value("type", "") == "page") {
                targetId = t.value("targetId", "");
                break;
            }
        }
    }
    if (targetId.empty()) {
        const json created = cdp_.call("Target.createTarget", {{"url", "about:blank"}}, "", err);
        targetId = created.is_object() ? created.value("targetId", "") : "";
    }
    if (targetId.empty()) {
        close();
        return "cannot open a tab: " + err;
    }
    const json attached =
        cdp_.call("Target.attachToTarget", {{"targetId", targetId}, {"flatten", true}}, "", err);
    session_ = attached.is_object() ? attached.value("sessionId", "") : "";
    if (session_.empty()) {
        close();
        return "cannot attach to the tab: " + err;
    }
    cdp_.call("Page.enable", json::object(), session_, err);
    return "";
}

std::string ChromeBrowser::navigate(const std::string& url) {
    if (!running()) return "browser is not running";
    std::string err;
    const json result = cdp_.call("Page.navigate", {{"url", url}}, session_, err);
    if (!err.empty()) return err;
    if (result.contains("errorText") && result["errorText"].is_string()) {
        return "could not open " + url + ": " + result["errorText"].get<std::string>();
    }
    return waitForLoad(30000);
}

std::string ChromeBrowser::waitForLoad(int timeoutMs) {
    for (int waited = 0; waited < timeoutMs; waited += 200) {
        std::string err;
        const json state = evaluate("document.readyState", err);
        if (err.empty() && state == "complete") return "";
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return "page did not finish loading in time";
}

json ChromeBrowser::evaluate(const std::string& script, std::string& error) {
    error.clear();
    if (!running()) {
        error = "browser is not running";
        return nullptr;
    }
    const json result = cdp_.call("Runtime.evaluate",
                                  {{"expression", script}, {"returnByValue", true}, {"awaitPromise", true}},
                                  session_, error);
    if (!error.empty()) return nullptr;
    if (result.contains("exceptionDetails")) {
        const json& d = result["exceptionDetails"];
        error = "page script failed: " +
                (d.is_object() && d.contains("text") && d["text"].is_string() ? d["text"].get<std::string>()
                                                                              : std::string("error"));
        return nullptr;
    }
    if (result.contains("result") && result["result"].is_object() && result["result"].contains("value")) {
        return result["result"]["value"];
    }
    return json();
}

void ChromeBrowser::close() {
    if (pid_ <= 0) {
        removeProfile();
        return;
    }
    if (cdp_.isOpen()) {
        std::string ignored;
        cdp_.call("Browser.close", json::object(), "", ignored, 3000);
        cdp_.close();
    }
    const pid_t pid = static_cast<pid_t>(pid_);
    bool exited = false;
    for (int i = 0; i < 50 && !exited; ++i) {   // wait up to ~5 s for Chrome to exit
        int status = 0;
        exited = waitpid(pid, &status, WNOHANG) == pid;
        if (!exited) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!exited) {
        ::kill(pid, SIGTERM);
        int status = 0;
        waitpid(pid, &status, 0);
    }
    pid_ = -1;
    port_ = 0;
    removeProfile();
}

bool ChromeBrowser::running() {
    if (pid_ <= 0) return false;
    int status = 0;
    if (waitpid(static_cast<pid_t>(pid_), &status, WNOHANG) == static_cast<pid_t>(pid_)) {
        pid_ = -1;   // Chrome exited (closed by the user, or crashed)
        cdp_.close();
        return false;
    }
    return true;
}

void ChromeBrowser::removeProfile() {
    if (profile_.empty()) return;
    std::error_code ec;
    // Only ever delete our own run folder inside ~/.makeiteasy/chrome-runs.
    const fs::path p = fs::weakly_canonical(profile_, ec);
    const fs::path runs = fs::weakly_canonical(agentRunsDir(), ec);
    if (!p.empty() && !runs.empty() && p.parent_path() == runs &&
        p.filename().string().rfind("run-", 0) == 0) {
        fs::remove_all(p, ec);
    }
    profile_.clear();
}

#else   // Windows for now

std::string ChromeBrowser::start() { return "browser control is not supported on this system yet"; }
bool ChromeBrowser::running() { return false; }
void ChromeBrowser::removeProfile() {}
std::string ChromeBrowser::navigate(const std::string&) { return "not supported"; }
std::string ChromeBrowser::waitForLoad(int) { return "not supported"; }
json ChromeBrowser::evaluate(const std::string&, std::string& error) {
    error = "not supported";
    return nullptr;
}
void ChromeBrowser::close() {}

#endif

}  // namespace easy::browser
