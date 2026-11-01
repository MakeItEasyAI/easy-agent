#include "browser/cdp.h"

#include <chrono>

namespace easy::browser {

using nlohmann::json;

std::string buildCdpCommand(int id, const std::string& method, const json& params,
                            const std::string& sessionId) {
    json command = {{"id", id}, {"method", method}, {"params", params.is_null() ? json::object() : params}};
    if (!sessionId.empty()) command["sessionId"] = sessionId;
    return command.dump();
}

json CdpClient::call(const std::string& method, const json& params, const std::string& sessionId,
                     std::string& error, int timeoutMs) {
    error.clear();
    const int id = nextId_++;
    if (std::string err = ws_.sendText(buildCdpCommand(id, method, params, sessionId));
        !err.empty()) {
        error = method + ": " + err;
        return nullptr;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (true) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                              deadline - std::chrono::steady_clock::now())
                              .count();
        if (left <= 0) {
            error = method + ": browser did not answer in time";
            return nullptr;
        }
        std::string text;
        if (std::string err = ws_.receiveText(text, static_cast<int>(left)); !err.empty()) {
            error = method + ": " + err;
            return nullptr;
        }
        const json message = json::parse(text, nullptr, /*allow_exceptions=*/false);
        if (message.is_discarded() || !message.is_object() || !message.contains("id") ||
            message["id"] != id) {
            continue;   // an event, or a reply to something else
        }
        if (message.contains("error")) {
            const json& e = message["error"];
            error = method + ": " +
                    (e.is_object() && e.contains("message") && e["message"].is_string()
                         ? e["message"].get<std::string>()
                         : std::string("unknown error"));
            return nullptr;
        }
        return message.contains("result") && message["result"].is_object() ? message["result"]
                                                                           : json::object();
    }
}

}  // namespace easy::browser
