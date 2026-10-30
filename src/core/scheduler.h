#pragma once

#include <string>

// Runs recipes at set times in the background. Calls the replay engine,
// never the AI directly.
namespace easy::core {

struct Schedule {
    std::string recipe_id;
    std::string when;      // e.g. "daily 18:00"
    bool enabled = true;
};

class Scheduler {
public:
    virtual ~Scheduler() = default;
    virtual void add(const Schedule& schedule) = 0;
    virtual void runForever() = 0;   // sleeps until the next job is due
};

}  // namespace easy::core
