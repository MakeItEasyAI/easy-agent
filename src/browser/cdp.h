#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "browser/websocket.h"

// Chrome DevTools Protocol client: send a command, wait for its reply.
// https://chromedevtools.github.io/devtools-protocol/
namespace easy::browser {

class CdpClient {
public:
    std::string connect(int port, const std::string& path) { return ws_.connect(port, path); }
    bool isOpen() const { return ws_.isOpen(); }
    void close() { ws_.close(); }

    // Sends {"id", "method", "params", "sessionId"} and waits for the reply
    // with the same id (events in between are skipped). On success returns
    // the "result" object; on failure returns null and sets `error`.
    nlohmann::json call(const std::string& method, const nlohmann::json& params,
                        const std::string& sessionId, std::string& error,
                        int timeoutMs = 30000);

private:
    WebSocket ws_;
    int nextId_ = 1;
};

// Exposed for tests: the JSON text of one command.
std::string buildCdpCommand(int id, const std::string& method, const nlohmann::json& params,
                            const std::string& sessionId);

}  // namespace easy::browser
