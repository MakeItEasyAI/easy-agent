#include "cli/console_confirmer.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace easy::cli {

bool ConsoleConfirmer::confirm(const std::string& question) {
    out_ << question << " [y/N] " << std::flush;

    std::string answer;
    if (!std::getline(in_, answer)) return false;   // no input (e.g. not a terminal) -> no

    // trim spaces, lowercase
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    answer.erase(answer.begin(), std::find_if(answer.begin(), answer.end(), notSpace));
    answer.erase(std::find_if(answer.rbegin(), answer.rend(), notSpace).base(), answer.end());
    std::transform(answer.begin(), answer.end(), answer.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return answer == "y" || answer == "yes";
}

std::string ConsoleConfirmer::askLine(const std::string& question) {
    out_ << question << " " << std::flush;
    std::string answer;
    if (!std::getline(in_, answer)) return "";
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    answer.erase(answer.begin(), std::find_if(answer.begin(), answer.end(), notSpace));
    answer.erase(std::find_if(answer.rbegin(), answer.rend(), notSpace).base(), answer.end());
    return answer;
}

}  // namespace easy::cli
