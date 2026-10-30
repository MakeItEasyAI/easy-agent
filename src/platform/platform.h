#pragma once

#include <optional>
#include <string>

// The only OS-specific layer. Everything above it is shared across
// macOS, Windows and Linux. Implementations live in mac/, windows/, linux/.
namespace easy::platform {

struct Point {
    double x = 0;
    double y = 0;
};

class Platform {
public:
    virtual ~Platform() = default;
    virtual bool openApp(const std::string& name) = 0;
    virtual bool openUrl(const std::string& url) = 0;
    virtual bool click(Point p) = 0;
    virtual bool typeText(const std::string& text) = 0;
    virtual std::optional<Point> findElement(const std::string& label) = 0;
};

}  // namespace easy::platform
