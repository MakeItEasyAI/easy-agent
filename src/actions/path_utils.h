#pragma once

#include <string>

// Small helpers shared by file actions.
namespace easy::actions {

// "~" or "~/x" -> "<home>" or "<home>/x". Other paths are returned unchanged.
std::string expandHome(const std::string& path);

// Simple wildcard match, case-insensitive:
//   *  matches any number of characters
//   ?  matches exactly one character
// Example: matchesPattern("Bill.PDF", "*.pdf") == true
bool matchesPattern(const std::string& name, const std::string& pattern);

}  // namespace easy::actions
