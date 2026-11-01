#pragma once

#include <string>

#include <nlohmann/json.hpp>

// Terminal-side client for the browser bridge. The `easy` session uses this to
// send one command (read_fields / fill / click) to the Chrome extension and get
// the result. It connects to a local Unix-domain socket (a file under
// ~/.makeiteasy/, user-only permissions) that the native-messaging host serves
// while Chrome + the extension are running. No network port is involved.
namespace easy::native {

std::string agentSocketPath();   // ~/.makeiteasy/agent.sock, expanded

class BrowserLink {
public:
    ~BrowserLink();

    // Connects to the bridge. On failure, `error` explains (usually: Chrome or
    // the extension is not running, so the bridge socket does not exist).
    bool open(std::string& error);

    // Sends a request like {"action":"read_fields"} and returns the page's
    // result (an array for read_fields, {ok,error} for fill/click).
    bool command(const nlohmann::json& request, nlohmann::json& result, std::string& error);

    void close();

private:
    int fd_ = -1;
    std::string buf_;
};

}  // namespace easy::native
