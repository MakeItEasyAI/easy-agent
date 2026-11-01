#pragma once

#include <cstdint>
#include <string>

// Minimal WebSocket client (RFC 6455) used only to talk to the agent's own
// Chrome over the DevTools protocol. Safety: connects to 127.0.0.1 ONLY and
// uses plain ws:// (traffic never leaves this computer).
namespace easy::browser {

class WebSocket {
public:
    WebSocket() = default;
    ~WebSocket();
    WebSocket(const WebSocket&) = delete;
    WebSocket& operator=(const WebSocket&) = delete;

    // Connects to ws://127.0.0.1:<port><path>. Returns "" or an error.
    std::string connect(int port, const std::string& path);
    bool isOpen() const { return fd_ >= 0; }

    std::string sendText(const std::string& text);   // "" or error
    // Waits up to timeoutMs for one complete text message. "" or error.
    std::string receiveText(std::string& out, int timeoutMs);
    void close();

private:
    std::string readSome(int timeoutMs);    // appends to buffer_
    std::string sendFrame(int opcode, const std::string& payload);

    int fd_ = -1;
    std::string buffer_;   // bytes received but not parsed yet
};

// ---- pure helpers (no network), exposed for tests ----

std::string base64Encode(const std::string& bytes);

// Builds one frame. Client frames must be masked (mask = 4 bytes).
std::string encodeFrame(int opcode, const std::string& payload, const std::string& mask);

struct Frame {
    bool fin = false;
    int opcode = 0;
    std::string payload;
};

// Parses one frame from the start of `buffer`. Returns false if more bytes are
// needed. On success sets `consumed` to the frame's size.
bool parseFrame(const std::string& buffer, Frame& frame, std::size_t& consumed);

}  // namespace easy::browser
