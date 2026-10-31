#pragma once

#include <iostream>

#include "core/confirmer.h"

// Asks in the terminal: "<question> [y/N] ". Only "y" or "yes" means yes.
// Anything else, an empty answer, or no input at all means NO (safe default).
namespace easy::cli {

class ConsoleConfirmer : public core::Confirmer {
public:
    ConsoleConfirmer(std::istream& in = std::cin, std::ostream& out = std::cout)
        : in_(in), out_(out) {}

    bool confirm(const std::string& question) override;

    // Asks for a line of text. Returns "" on empty answer or no input.
    std::string askLine(const std::string& question);

private:
    std::istream& in_;
    std::ostream& out_;
};

}  // namespace easy::cli
