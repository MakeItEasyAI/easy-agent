#include "actions/move_files.h"

#include <filesystem>
#include <set>
#include <sstream>
#include <system_error>

#include "actions/path_utils.h"

namespace easy::actions {

namespace fs = std::filesystem;

namespace {

// "a\nb\n\nc" -> {"a", "b", "c"} (empty lines ignored, "~" expanded)
std::vector<fs::path> splitFiles(const std::string& text) {
    std::vector<fs::path> files;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) files.emplace_back(expandHome(line));
    }
    return files;
}

// Move one file. rename() is instant but fails across disks, so fall back
// to copy + delete in that case.
std::error_code moveOne(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::rename(from, to, ec);
    if (!ec) return ec;

    ec.clear();
    fs::copy_file(from, to, fs::copy_options::none, ec);   // none = never overwrite
    if (ec) return ec;
    fs::remove(from, ec);
    return ec;
}

}  // namespace

ActionResult MoveFilesAction::run(const Params& params) {
    const auto filesIt = params.find("files");
    const auto toIt = params.find("to");
    if (filesIt == params.end()) return {false, "move_files: missing \"files\"", {}};
    if (toIt == params.end() || toIt->second.empty())
        return {false, "move_files: missing \"to\"", {}};

    const std::vector<fs::path> files = splitFiles(filesIt->second);
    const fs::path dest = expandHome(toIt->second);

    if (files.empty()) return {true, "moved 0 file(s)", {}};   // nothing to do

    std::error_code ec;
    if (fs::exists(dest, ec) && !fs::is_directory(dest, ec))
        return {false, "move_files: destination is not a folder: " + dest.string(), {}};

    // ---- Phase 1: check everything, change nothing ----
    std::set<std::string> targetNames;
    for (const fs::path& file : files) {
        if (!fs::is_regular_file(file, ec))
            return {false, "move_files: not a file: " + file.string(), {}};

        const fs::path target = dest / file.filename();
        if (fs::exists(target, ec))
            return {false, "move_files: already exists, not overwriting: " + target.string(), {}};
        if (!targetNames.insert(file.filename().string()).second)
            return {false, "move_files: two files have the same name: " +
                               file.filename().string(), {}};
    }

    // ---- Phase 2: do the work ----
    fs::create_directories(dest, ec);
    if (ec) return {false, "move_files: cannot create " + dest.string() + ": " + ec.message(), {}};

    std::vector<std::string> moved;
    for (const fs::path& file : files) {
        const fs::path target = dest / file.filename();
        if (const std::error_code moveEc = moveOne(file, target)) {
            return {false, "move_files: failed on " + file.string() + ": " + moveEc.message() +
                               " (" + std::to_string(moved.size()) + " moved before this)",
                    std::move(moved)};
        }
        moved.push_back(target.string());
    }

    return {true, "moved " + std::to_string(moved.size()) + " file(s) to " + dest.string(),
            std::move(moved)};
}

bool MoveFilesAction::verify(const Params& params) {
    const auto filesIt = params.find("files");
    const auto toIt = params.find("to");
    if (filesIt == params.end() || toIt == params.end()) return false;

    const fs::path dest = expandHome(toIt->second);
    std::error_code ec;
    for (const fs::path& file : splitFiles(filesIt->second)) {
        if (fs::exists(file, ec)) return false;                       // still in old place
        if (!fs::is_regular_file(dest / file.filename(), ec)) return false;   // not arrived
    }
    return true;
}

}  // namespace easy::actions
