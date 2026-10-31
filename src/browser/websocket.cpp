#include "browser/websocket.h"

#include <random>

#if defined(__APPLE__) || defined(__linux__)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#define EASY_HAVE_SOCKETS 1
#endif

namespace easy::browser {

namespace {

#if defined(__linux__)
constexpr int kSendFlags = MSG_NOSIGNAL;   // closed connection -> error, not SIGPIPE
#else
constexpr int kSendFlags = 0;              // macOS uses SO_NOSIGPIPE on the socket instead
#endif

constexpr std::size_t kMaxMessage = 64 * 1024 * 1024;   // safety limit: 64 MB

std::string randomBytes(std::size_t n) {
    static std::random_device rd;
    std::string out(n, '\0');
    for (char& c : out) c = static_cast<char>(rd() & 0xFF);
    return out;
}

}  // namespace

// ---------------- pure helpers ----------------

std::string base64Encode(const std::string& bytes) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const unsigned n = (static_cast<unsigned char>(bytes[i]) << 16) |
                           (static_cast<unsigned char>(bytes[i + 1]) << 8) |
                           static_cast<unsigned char>(bytes[i + 2]);
        out += table[(n >> 18) & 63];
        out += table[(n >> 12) & 63];
        out += table[(n >> 6) & 63];
        out += table[n & 63];
    }
    if (i + 1 == bytes.size()) {
        const unsigned n = static_cast<unsigned char>(bytes[i]) << 16;
        out += table[(n >> 18) & 63];
        out += table[(n >> 12) & 63];
        out += "==";
    } else if (i + 2 == bytes.size()) {
        const unsigned n = (static_cast<unsigned char>(bytes[i]) << 16) |
                           (static_cast<unsigned char>(bytes[i + 1]) << 8);
        out += table[(n >> 18) & 63];
        out += table[(n >> 12) & 63];
        out += table[(n >> 6) & 63];
        out += '=';
    }
    return out;
}

std::string encodeFrame(int opcode, const std::string& payload, const std::string& mask) {
    std::string frame;
    frame += static_cast<char>(0x80 | (opcode & 0x0F));   // FIN + opcode
    const std::uint64_t len = payload.size();
    const char maskBit = mask.size() == 4 ? static_cast<char>(0x80) : 0;
    if (len < 126) {
        frame += static_cast<char>(maskBit | static_cast<char>(len));
    } else if (len <= 0xFFFF) {
        frame += static_cast<char>(maskBit | 126);
        frame += static_cast<char>((len >> 8) & 0xFF);
        frame += static_cast<char>(len & 0xFF);
    } else {
        frame += static_cast<char>(maskBit | 127);
        for (int shift = 56; shift >= 0; shift -= 8) frame += static_cast<char>((len >> shift) & 0xFF);
    }
    if (mask.size() == 4) {
        frame += mask;
        for (std::size_t i = 0; i < payload.size(); ++i) frame += static_cast<char>(payload[i] ^ mask[i % 4]);
    } else {
        frame += payload;
    }
    return frame;
}

bool parseFrame(const std::string& buffer, Frame& frame, std::size_t& consumed) {
    if (buffer.size() < 2) return false;
    const auto b0 = static_cast<unsigned char>(buffer[0]);
    const auto b1 = static_cast<unsigned char>(buffer[1]);
    frame.fin = (b0 & 0x80) != 0;
    frame.opcode = b0 & 0x0F;
    const bool masked = (b1 & 0x80) != 0;
    std::uint64_t len = b1 & 0x7F;
    std::size_t pos = 2;

    if (len == 126) {
        if (buffer.size() < 4) return false;
        len = (static_cast<unsigned char>(buffer[2]) << 8) | static_cast<unsigned char>(buffer[3]);
        pos = 4;
    } else if (len == 127) {
        if (buffer.size() < 10) return false;
        len = 0;
        for (int i = 0; i < 8; ++i) len = (len << 8) | static_cast<unsigned char>(buffer[2 + i]);
        pos = 10;
    }
    std::string mask;
    if (masked) {
        if (buffer.size() < pos + 4) return false;
        mask = buffer.substr(pos, 4);
        pos += 4;
    }
    if (len > kMaxMessage) {   // refuse absurd sizes instead of allocating them
        frame.opcode = -1;
        consumed = buffer.size();
        return true;
    }
    if (buffer.size() < pos + len) return false;

    frame.payload = buffer.substr(pos, static_cast<std::size_t>(len));
    if (masked) {
        for (std::size_t i = 0; i < frame.payload.size(); ++i) frame.payload[i] ^= mask[i % 4];
    }
    consumed = pos + static_cast<std::size_t>(len);
    return true;
}

// ---------------- network ----------------

WebSocket::~WebSocket() { close(); }

#if defined(EASY_HAVE_SOCKETS)

std::string WebSocket::connect(int port, const std::string& path) {
    close();
    if (port <= 0 || port > 65535) return "bad port";
    if (path.empty() || path[0] != '/') return "bad path";

    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) return "cannot create socket";
#if defined(__APPLE__)
    // If Chrome dies, writing must return an error, not kill the agent (SIGPIPE).
    int on = 1;
    ::setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // 127.0.0.1 only, by design
    if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close();
        return "cannot connect to 127.0.0.1:" + std::to_string(port);
    }

    const std::string request = "GET " + path + " HTTP/1.1\r\n"
                                "Host: 127.0.0.1:" + std::to_string(port) + "\r\n"
                                "Upgrade: websocket\r\n"
                                "Connection: Upgrade\r\n"
                                "Sec-WebSocket-Key: " + base64Encode(randomBytes(16)) + "\r\n"
                                "Sec-WebSocket-Version: 13\r\n\r\n";
    if (::send(fd_, request.data(), request.size(), kSendFlags) != static_cast<ssize_t>(request.size())) {
        close();
        return "handshake send failed";
    }

    // Read the HTTP response headers.
    std::size_t end;
    while ((end = buffer_.find("\r\n\r\n")) == std::string::npos) {
        if (std::string err = readSome(5000); !err.empty()) {
            close();
            return "handshake failed: " + err;
        }
        if (buffer_.size() > 16384) {
            close();
            return "handshake response too large";
        }
    }
    const std::string headers = buffer_.substr(0, end);
    buffer_.erase(0, end + 4);
    if (headers.rfind("HTTP/1.1 101", 0) != 0) {
        close();
        return "browser refused the connection";
    }
    return "";
}

std::string WebSocket::readSome(int timeoutMs) {
    pollfd p{fd_, POLLIN, 0};
    const int ready = ::poll(&p, 1, timeoutMs);
    if (ready == 0) return "timed out";
    if (ready < 0) return "poll failed";
    char chunk[16384];
    const ssize_t n = ::recv(fd_, chunk, sizeof(chunk), 0);
    if (n <= 0) return "connection closed";
    buffer_.append(chunk, static_cast<std::size_t>(n));
    return "";
}

std::string WebSocket::sendFrame(int opcode, const std::string& payload) {
    if (fd_ < 0) return "not connected";
    const std::string frame = encodeFrame(opcode, payload, randomBytes(4));
    std::size_t sent = 0;
    while (sent < frame.size()) {
        const ssize_t n = ::send(fd_, frame.data() + sent, frame.size() - sent, kSendFlags);
        if (n <= 0) return "send failed";
        sent += static_cast<std::size_t>(n);
    }
    return "";
}

std::string WebSocket::sendText(const std::string& text) { return sendFrame(0x1, text); }

std::string WebSocket::receiveText(std::string& out, int timeoutMs) {
    out.clear();
    std::string message;
    while (true) {
        Frame frame;
        std::size_t consumed = 0;
        while (!parseFrame(buffer_, frame, consumed)) {
            if (std::string err = readSome(timeoutMs); !err.empty()) return err;
        }
        buffer_.erase(0, consumed);

        switch (frame.opcode) {
            case -1: return "message too large";
            case 0x8: close(); return "connection closed by browser";
            case 0x9: sendFrame(0xA, frame.payload); continue;   // ping -> pong
            case 0xA: continue;                                   // pong
            case 0x0: case 0x1: case 0x2:
                message += frame.payload;
                if (message.size() > kMaxMessage) return "message too large";
                if (frame.fin) {
                    out = std::move(message);
                    return "";
                }
                continue;
            default: return "unexpected frame";
        }
    }
}

void WebSocket::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    buffer_.clear();
}

#else   // no POSIX sockets (Windows for now)

std::string WebSocket::connect(int, const std::string&) { return "browser control is not supported on this system yet"; }
std::string WebSocket::readSome(int) { return "not supported"; }
std::string WebSocket::sendFrame(int, const std::string&) { return "not supported"; }
std::string WebSocket::sendText(const std::string&) { return "not supported"; }
std::string WebSocket::receiveText(std::string&, int) { return "not supported"; }
void WebSocket::close() {}

#endif

}  // namespace easy::browser
