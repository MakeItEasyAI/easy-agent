#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "browser/cdp.h"

// The agent's own Chrome window, controlled over the DevTools protocol.
//
// Safety:
//   - A SEPARATE Chrome with a FRESH profile for every run
//     (~/.makeiteasy/chrome-runs/run-<id>), deleted when the run ends:
//     none of your normal logins, cookies or history, and nothing is left behind.
//   - Remote control port is chosen at random by Chrome (--remote-debugging-port=0)
//     and listens on 127.0.0.1 only. It has no password, so it is open only
//     while the agent's command runs, then Chrome is closed.
//   - Your normal Chrome is never touched.
namespace easy::browser {

class ChromeBrowser {
public:
    ChromeBrowser() = default;
    ~ChromeBrowser() { close(); }
    ChromeBrowser(const ChromeBrowser&) = delete;
    ChromeBrowser& operator=(const ChromeBrowser&) = delete;

    std::string start();                               // "" or error
    bool running();                                    // false once Chrome has exited
    int port() const { return port_; }
    const std::string& profileDir() const { return profile_; }

    std::string navigate(const std::string& url);      // waits for the page to load
    // Runs JavaScript in the page; returns its value. Sets `error` on failure.
    nlohmann::json evaluate(const std::string& script, std::string& error);

    void close();                                      // closes Chrome, deletes the run's profile

private:
    std::string waitForLoad(int timeoutMs);
    void removeProfile();

    long pid_ = -1;
    int port_ = 0;
    std::string profile_;   // this run's profile folder
    CdpClient cdp_;
    std::string session_;   // the page's DevTools session
};

std::string findChrome();          // path to the Chrome program, or ""
std::string agentRunsDir();        // ~/.makeiteasy/chrome-runs (expanded)
std::string chromeLogPath();       // ~/.makeiteasy/chrome.log (Chrome's own messages)

}  // namespace easy::browser
