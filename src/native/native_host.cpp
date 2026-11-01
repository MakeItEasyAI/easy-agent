#include "native/native_host.h"

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "native/browser_link.h"   // agentSocketPath()

#if defined(__APPLE__) || defined(__linux__)
#include <poll.h>
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

// Sends a command to the extension and waits for the matching {type:result,id}.
// Skips anything else Chrome sends. Returns false if Chrome's pipe closed.
bool askChrome(int id, const json& request, json& result) {
    json cmd = request;
    cmd["type"] = "command";
    cmd["id"] = id;
    if (!writeToChrome(cmd)) return false;
    while (true) {
        auto m = readFromChrome();
        if (!m) return false;
        if (m->value("type", "") == "result" && m->value("id", -1) == id) {
            result = m->value("result", json());
            return true;
        }
        // ignore unrelated messages (e.g. a hello/ping)
    }
}

int runBridge() {
    const std::string path = agentSocketPath();
    ::unlink(path.c_str());

    const int listenFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listenFd < 0) return 1;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());
    if (::bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        // Another bridge already owns the socket (e.g. this process was started
        // just to answer a one-shot ping). Serve ping over stdio and exit.
        ::close(listenFd);
        while (auto m = readFromChrome()) {
            if (m->value("type", "") == "ping") writeToChrome({{"type", "pong"}, {"agent", "easy"}});
        }
        return 0;
    }
    ::chmod(path.c_str(), 0600);   // only this user
    ::listen(listenFd, 4);

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
            if (m->value("type", "") == "ping") writeToChrome({{"type", "pong"}, {"agent", "easy"}});
        }
        if (fds[1].revents & POLLIN) {
            const int c = ::accept(listenFd, nullptr, nullptr);
            if (c >= 0) {
                if (client >= 0) ::close(client);   // one terminal at a time
                client = c;
                clientBuf.clear();
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
                if (!askChrome(nextId++, req, result)) {
                    sendLine(client, {{"error", "browser closed"}});
                    ::unlink(path.c_str());
                    return 0;   // Chrome gone
                }
                sendLine(client, {{"result", result}});
            }
        }
    }
    ::unlink(path.c_str());
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
