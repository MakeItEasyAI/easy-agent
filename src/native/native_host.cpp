#include "native/native_host.h"

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "native/browser_link.h"   // agentSocketPath()

#if defined(__APPLE__) || defined(__linux__)
#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#define EASY_HAVE_BRIDGE 1
#endif
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

// The native host is a small BRIDGE, launched by Chrome when the extension
// connects. It talks to Chrome over stdin/stdout (native messaging) and serves
// a local Unix-domain socket that the terminal `easy` connects to. So:
//
//   terminal easy  ──unix socket──►  this bridge  ──stdin/stdout──►  extension  ──►  page
//
// No network port is opened. The socket file is user-only (0600).
namespace easy::native {

using nlohmann::json;

namespace {

constexpr std::uint32_t kMaxMessage = 64u * 1024 * 1024;

#if defined(EASY_HAVE_BRIDGE)

// Diagnostic log file next to the socket (~/.makeiteasy/bridge.log). The native
// host must never log to stdout (reserved for the Chrome protocol), so we
// append to a file instead. The extension forwards its own logs here too, so
// one file shows the whole terminal<->bridge<->extension story.
std::string bridgeLogPath() {
    const std::string s = agentSocketPath();
    const auto slash = s.find_last_of('/');
    return (slash == std::string::npos ? std::string(".") : s.substr(0, slash)) + "/bridge.log";
}

void logToFile(const std::string& msg) {
    std::FILE* f = std::fopen(bridgeLogPath().c_str(), "a");
    if (!f) return;
    std::time_t t = std::time(nullptr);
    char ts[32];
    std::strftime(ts, sizeof(ts), "%H:%M:%S", std::localtime(&t));
    std::fprintf(f, "[%s] %s\n", ts, msg.c_str());
    std::fclose(f);
}

bool readExact(int fd, void* buf, std::size_t n) {
    auto* p = static_cast<unsigned char*>(buf);
    std::size_t got = 0;
    while (got < n) {
        const ssize_t r = ::read(fd, p + got, n - got);
        if (r <= 0) return false;   // EOF or error
        got += static_cast<std::size_t>(r);
    }
    return true;
}

// Reads one native-messaging message from Chrome (fd 0): 4-byte LE length + JSON.
std::optional<json> readFromChrome() {
    unsigned char len[4];
    if (!readExact(0, len, 4)) return std::nullopt;
    const std::uint32_t n = static_cast<std::uint32_t>(len[0]) |
                            (static_cast<std::uint32_t>(len[1]) << 8) |
                            (static_cast<std::uint32_t>(len[2]) << 16) |
                            (static_cast<std::uint32_t>(len[3]) << 24);
    if (n == 0 || n > kMaxMessage) return std::nullopt;
    std::string buf(n, '\0');
    if (!readExact(0, buf.data(), n)) return std::nullopt;
    json j = json::parse(buf, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) return std::nullopt;
    return j;
}

bool writeToChrome(const json& message) {
    const std::string s = message.dump();
    const std::uint32_t n = static_cast<std::uint32_t>(s.size());
    unsigned char hdr[4] = {static_cast<unsigned char>(n & 0xFF),
                            static_cast<unsigned char>((n >> 8) & 0xFF),
                            static_cast<unsigned char>((n >> 16) & 0xFF),
                            static_cast<unsigned char>((n >> 24) & 0xFF)};
    std::string out(reinterpret_cast<char*>(hdr), 4);
    out += s;
    std::size_t sent = 0;
    while (sent < out.size()) {
        const ssize_t w = ::write(1, out.data() + sent, out.size() - sent);
        if (w <= 0) return false;
        sent += static_cast<std::size_t>(w);
    }
    return true;
}

bool sendLine(int fd, const json& message) {
    const std::string line = message.dump() + "\n";
    std::size_t sent = 0;
    while (sent < line.size()) {
        const ssize_t w = ::send(fd, line.data() + sent, line.size() - sent, 0);
        if (w <= 0) return false;
        sent += static_cast<std::size_t>(w);
    }
    return true;
}

// Handles non-result messages from the extension: records its logs so they
// land in bridge.log, and answers pings.
void handleAux(const json& m) {
    const std::string type = m.value("type", "");
    if (type == "log") logToFile("ext: " + m.value("text", std::string()));
    else if (type == "ping") writeToChrome({{"type", "pong"}, {"agent", "easy"}});
}

// Result of asking the extension to run one command.
enum class Ask { Ok, Timeout, Closed };

// Sends a command to the extension and waits for the matching {type:result,id}.
// Skips anything else Chrome sends (keepalive/pong/log). Times out rather than
// blocking forever if the extension's worker never replies (e.g. it was put to
// sleep mid-command), so the terminal always gets an answer.
Ask askChrome(int id, const json& request, json& result) {
    json cmd = request;
    cmd["type"] = "command";
    cmd["id"] = id;
    logToFile("-> extension: action=" + request.value("action", std::string("?")) +
              " id=" + std::to_string(id));
    if (!writeToChrome(cmd)) return Ask::Closed;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(25);
    while (true) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) { logToFile("!! TIMEOUT waiting for extension id=" + std::to_string(id)); return Ask::Timeout; }
        const auto remainingMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        pollfd p{0, POLLIN, 0};
        const int r = ::poll(&p, 1, static_cast<int>(remainingMs));
        if (r < 0) return Ask::Closed;
        if (r == 0) { logToFile("!! TIMEOUT (poll) waiting for extension id=" + std::to_string(id)); return Ask::Timeout; }
        auto m = readFromChrome();
        if (!m) { logToFile("!! Chrome pipe closed while waiting id=" + std::to_string(id)); return Ask::Closed; }
        if (m->value("type", "") == "result" && m->value("id", -1) == id) {
            logToFile("<- extension replied id=" + std::to_string(id));
            result = m->value("result", json());
            return Ask::Ok;
        }
        handleAux(*m);   // log/ping while we wait
    }
}

// Answers pings over stdio until Chrome closes the pipe. Used by a non-owner
// bridge so a one-shot status check still gets a reply without touching the
// socket the owner holds.
void servePingsThenExit() {
    while (auto m = readFromChrome()) {
        if (m->value("type", "") == "ping") writeToChrome({{"type", "pong"}, {"agent", "easy"}});
    }
}

int runBridge() {
    const std::string path = agentSocketPath();
    const std::string lockPath = path + ".lock";

    // Singleton guard. Chrome may spawn several native-host processes (initial
    // connect, onDisconnect retry, keep-alive alarm). Only the one holding this
    // lock owns the socket; the rest answer pings and stay out of the way, so
    // they never unlink a live socket out from under the owner.
    const int lockFd = ::open(lockPath.c_str(), O_CREAT | O_RDWR, 0600);
    if (lockFd < 0) { servePingsThenExit(); return 0; }
    if (::flock(lockFd, LOCK_EX | LOCK_NB) != 0) {
        ::close(lockFd);
        logToFile("bridge ping-only (another bridge owns the socket)");
        servePingsThenExit();   // another bridge owns the socket
        return 0;
    }

    ::unlink(path.c_str());
    const int listenFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listenFd < 0) { ::flock(lockFd, LOCK_UN); ::close(lockFd); return 1; }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());
    if (::bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(listenFd);
        ::flock(lockFd, LOCK_UN);
        ::close(lockFd);
        servePingsThenExit();
        return 0;
    }
    ::chmod(path.c_str(), 0600);   // only this user
    ::listen(listenFd, 4);
    logToFile("bridge OWNER up, serving socket");

    int client = -1;
    std::string clientBuf;
    int nextId = 1;

    while (true) {
        pollfd fds[3];
        int n = 0;
        fds[n++] = {0, POLLIN, 0};          // Chrome pipe (for EOF / stray messages)
        fds[n++] = {listenFd, POLLIN, 0};
        if (client >= 0) fds[n++] = {client, POLLIN, 0};
        const int ready = ::poll(fds, n, 20000);   // 20s: also our heartbeat tick
        if (ready < 0) break;
        if (ready == 0) {
            // Idle: send a heartbeat so Chrome keeps the extension's worker (and
            // this bridge) alive instead of putting it to sleep.
            if (!writeToChrome({{"type", "keepalive"}})) break;
            continue;
        }

        if (fds[0].revents & POLLIN) {
            auto m = readFromChrome();
            if (!m) break;   // Chrome / extension closed: shut down the bridge
            handleAux(*m);   // record extension logs / answer pings
        }
        if (fds[1].revents & POLLIN) {
            const int c = ::accept(listenFd, nullptr, nullptr);
            if (c >= 0) {
                if (client >= 0) ::close(client);   // one terminal at a time
                client = c;
                clientBuf.clear();
                logToFile("terminal connected");
            }
        }
        if (client >= 0 && n == 3 && (fds[2].revents & POLLIN)) {
            char chunk[8192];
            const ssize_t r = ::recv(client, chunk, sizeof(chunk), 0);
            if (r <= 0) {
                ::close(client);
                client = -1;
                continue;
            }
            clientBuf.append(chunk, static_cast<std::size_t>(r));
            std::size_t nl;
            while ((nl = clientBuf.find('\n')) != std::string::npos) {
                const std::string line = clientBuf.substr(0, nl);
                clientBuf.erase(0, nl + 1);
                const json req = json::parse(line, nullptr, /*allow_exceptions=*/false);
                if (req.is_discarded()) {
                    sendLine(client, {{"error", "bad request"}});
                    continue;
                }
                json result;
                const Ask st = askChrome(nextId++, req, result);
                if (st == Ask::Ok) {
                    sendLine(client, {{"result", result}});
                } else if (st == Ask::Timeout) {
                    // Worker didn't answer; keep serving so the next try works.
                    sendLine(client, {{"error", "the browser extension did not respond (it may "
                                               "be asleep). Keep the form tab in front and retry."}});
                } else {
                    sendLine(client, {{"error", "browser closed"}});
                    goto cleanup;   // Chrome gone
                }
            }
        }
    }

cleanup:
    if (client >= 0) ::close(client);
    ::close(listenFd);
    ::unlink(path.c_str());
    ::flock(lockFd, LOCK_UN);
    ::close(lockFd);
    return 0;
}

#endif  // EASY_HAVE_BRIDGE

}  // namespace

int runNativeHost() {
#if defined(_WIN32)
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    return 1;   // the bridge is POSIX-only for now
#elif defined(EASY_HAVE_BRIDGE)
    return runBridge();
#else
    return 1;
#endif
}

}  // namespace easy::native
