// Systems without PDF text extraction yet (Windows / Linux for now).
#include "platform/pdf_text.h"

namespace easy::platform {

PdfText readPdfText(const std::string&, int, std::size_t) {
    PdfText out;
    out.error = "reading PDFs is only supported on macOS for now";
    return out;
}

}  // namespace easy::platform
