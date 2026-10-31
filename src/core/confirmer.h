#pragma once

#include <string>

// Asks the user a yes/no question. The replay engine uses this when the
// safety guard says AskUser. Tests use a fake that answers automatically.
namespace easy::core {

class Confirmer {
public:
    virtual ~Confirmer() = default;
    virtual bool confirm(const std::string& question) = 0;   // true = user said yes
};

}  // namespace easy::core
