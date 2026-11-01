#include "native/native_host.h"

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

namespace easy::native {

using nlohmann::json;

namespace {

constexpr std::uint32_t kMaxMessage = 64u * 1024 * 1024;   // 64 MB safety limit

// Chrome's framing: a 4-byte little-endian length, then that many UTF-8 bytes.
std::optional<json> readMessage() {
    unsigned char len[4];
    if (std::fread(len, 1, 4, stdin) != 4) return std::nullopt;   // EOF: Chrome closed the pipe
    const std::uint32_t n = static_cast<std::uint32_t>(len[0]) |
                            (static_cast<std::uint32_t>(len[1]) << 8) |
                            (static_cast<std::uint32_t>(len[2]) << 16) |
                            (static_cast<std::uint32_t>(len[3]) << 24);
    if (n == 0 || n > kMaxMessage) return std::nullopt;
    std::string buf(n, '\0');
    if (std::fread(buf.data(), 1, n, stdin) != n) return std::nullopt;
    json j = json::parse(buf, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) return std::nullopt;
    return j;
}

void writeMessage(const json& message) {
    const std::string s = message.dump();
    const std::uint32_t n = static_cast<std::uint32_t>(s.size());
    unsigned char len[4] = {static_cast<unsigned char>(n & 0xFF),
                            static_cast<unsigned char>((n >> 8) & 0xFF),
                            static_cast<unsigned char>((n >> 16) & 0xFF),
                            static_cast<unsigned char>((n >> 24) & 0xFF)};
    std::fwrite(len, 1, 4, stdout);
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fflush(stdout);
}

}  // namespace

int runNativeHost() {
#if defined(_WIN32)
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    while (auto message = readMessage()) {
        const json m = *message;
        const std::string type = m.value("type", "");

        if (type == "ping") {
            writeMessage({{"type", "pong"}, {"agent", "easy"}, {"version", EASY_VERSION}});
        } else if (type == "task") {
            // Increment 2 will plan the task and drive the extension. For now,
            // confirm the pipe works.
            writeMessage({{"type", "done"},
                          {"ok", false},
                          {"message", "Agent reached. Filling forms from the popup is not built "
                                      "yet (you received: \"" + m.value("task", "") + "\")."}});
        } else {
            writeMessage({{"type", "error"}, {"message", "unknown message type: " + type}});
        }
    }
    return 0;
}

}  // namespace easy::native
