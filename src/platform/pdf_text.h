#pragma once

#include <string>

// Extracts the text of a PDF file (no network, no AI).
//   macOS: Apple PDFKit (built in).   Other systems: not supported yet.
namespace easy::platform {

struct PdfText {
    bool ok = false;
    std::string text;
    int pages = 0;          // pages read
    int totalPages = 0;     // pages in the file
    bool truncated = false; // stopped at the page / size limit
    std::string error;
};

PdfText readPdfText(const std::string& path, int maxPages = 30, std::size_t maxChars = 60000);

}  // namespace easy::platform
