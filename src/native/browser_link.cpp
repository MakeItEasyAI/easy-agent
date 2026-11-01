#include "native/browser_link.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "actions/path_utils.h"

#if defined(__APPLE__) || defined(__linux__)
#include <cerrno>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#define EASY_HAVE_UNIX_SOCKETS 1
#endif

namespace easy::native {

using nlohmann::json;

std::string agentSocketPath() { return actions::expandHome("~/.makeiteasy/agent.sock"); }

BrowserLink::~BrowserLink() { close(); }

#if defined(EASY_HAVE_UNIX_SOCKETS)

bool BrowserLink::open(std::string& error) {
    const std::string path = agentSocketPath();
    if (path.size() >= sizeof(sockaddr_un{}.sun_path)) {
        error = "socket path too long";
        return false;
    }
    // Chrome may have let the extension's service worker sleep, which takes the
    // bridge (and this socket) down with it. A keep-alive alarm in the
    // extension wakes it within ~30s, so wait and retry rather than failing the
    // instant the socket is missing.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(40);
    bool announced = false;
    for (;;) {
        fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd_ < 0) {
            error = "could not create a socket";
            return false;
        }
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());
        if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
            // Don't let a silent bridge block recv() forever; wake every 2s so
            // command() can enforce its own deadline.
            timeval tv{2, 0};
            ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            return true;
        }
        close();   // resets fd_ to -1 before the next attempt
        if (std::chrono::steady_clock::now() >= deadline) {
            error = "the browser bridge is not running. Open Chrome with the easy-agent extension "
                    "loaded, and a form in the front tab, then try again.";
            return false;
        }
        if (!announced) {
            std::cerr << "Waking the Chrome extension (up to ~30s)...\n";
            announced = true;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

bool BrowserLink::command(const json& request, json& result, std::string& error) {
    if (fd_ < 0) {
        error = "not connected";
        return false;
    }
    const std::string line = request.dump() + "\n";
    std::size_t sent = 0;
    while (sent < line.size()) {
        const ssize_t n = ::send(fd_, line.data() + sent, line.size() - sent, 0);
        if (n <= 0) {
            error = "lost connection to the browser bridge";
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }

    // Read until a newline (one JSON response). The bridge answers within ~25s
    // even when the extension is asleep, so bound the wait and report cleanly
    // instead of hanging forever.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
    std::size_t nl;
    while ((nl = buf_.find('\n')) == std::string::npos) {
        char chunk[8192];
        const ssize_t n = ::recv(fd_, chunk, sizeof(chunk), 0);
        if (n > 0) {
            buf_.append(chunk, static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0) {
            error = "the browser bridge closed the connection";
            return false;
        }
        // n < 0: a 2s recv timeout (EAGAIN) just lets us re-check the deadline.
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (std::chrono::steady_clock::now() > deadline) {
                error = "the browser did not respond in time (reload the extension and keep the "
                        "form tab in front)";
                return false;
            }
            continue;
        }
        error = "lost connection to the browser bridge";
        return false;
    }
    const std::string response = buf_.substr(0, nl);
    buf_.erase(0, nl + 1);

    const json j = json::parse(response, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) {
        error = "the browser bridge sent a bad response";
        return false;
    }
    if (j.contains("error") && j["error"].is_string() && !j["error"].get<std::string>().empty()) {
        error = j["error"].get<std::string>();
        return false;
    }
    result = j.value("result", json());
    return true;
}

void BrowserLink::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    buf_.clear();
}

#else   // Windows: not supported yet

bool BrowserLink::open(std::string& error) {
    error = "the browser bridge is not supported on this system yet";
    return false;
}
bool BrowserLink::command(const json&, json&, std::string& error) {
    error = "not supported";
    return false;
}
void BrowserLink::close() {}

#endif

}  // namespace easy::native
