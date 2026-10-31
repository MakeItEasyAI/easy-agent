#include "actions/list_files.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

#include "actions/path_utils.h"

namespace easy::actions {

namespace fs = std::filesystem;

ActionResult ListFilesAction::run(const Params& params) {
    const auto folderIt = params.find("folder");
    if (folderIt == params.end() || folderIt->second.empty()) {
        return {false, "list_files: missing \"folder\"", {}};
    }

    const auto patternIt = params.find("pattern");
    const std::string pattern = (patternIt != params.end()) ? patternIt->second : "*";
    const fs::path folder = expandHome(folderIt->second);

    // error_code versions never throw: problems come back in `ec`.
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
        return {false, "list_files: not a folder: " + folder.string(), {}};
    }

    std::vector<std::string> files;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::directory_entry& entry = *it;
        const std::string fileName = entry.path().filename().string();

        if (fileName.empty() || fileName[0] == '.') continue;   // skip hidden files
        std::error_code typeEc;
        if (!entry.is_regular_file(typeEc)) continue;           // skip folders, links to folders
        if (!matchesPattern(fileName, pattern)) continue;

        files.push_back(entry.path().string());
    }
    if (ec) {
        return {false, "list_files: cannot read " + folder.string() + ": " + ec.message(), {}};
    }

    std::sort(files.begin(), files.end());   // same order every run
    return {true, "found " + std::to_string(files.size()) + " file(s)", std::move(files)};
}

bool ListFilesAction::verify(const Params& /*params*/) {
    return true;   // read-only: nothing on disk changed, so nothing to check
}

}  // namespace easy::actions
