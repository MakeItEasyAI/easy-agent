// macOS: PDF text through Apple PDFKit. Compiled as Objective-C++ with ARC.
#import <Foundation/Foundation.h>
#import <PDFKit/PDFKit.h>

#include "platform/pdf_text.h"

namespace easy::platform {

PdfText readPdfText(const std::string& path, int maxPages, std::size_t maxChars) {
    PdfText out;
    @autoreleasepool {
        NSString* file = [NSString stringWithUTF8String:path.c_str()];
        if (file == nil) {
            out.error = "bad file name";
            return out;
        }
        PDFDocument* doc = [[PDFDocument alloc] initWithURL:[NSURL fileURLWithPath:file]];
        if (doc == nil) {
            out.error = "cannot open as a PDF: " + path;
            return out;
        }
        if (doc.isLocked) {
            out.error = "the PDF is password-protected; easy does not unlock files";
            return out;
        }

        out.totalPages = static_cast<int>(doc.pageCount);
        for (NSInteger i = 0; i < doc.pageCount; ++i) {
            if (out.pages >= maxPages || out.text.size() >= maxChars) {
                out.truncated = true;
                break;
            }
            NSString* pageText = [doc pageAtIndex:i].string;
            if (pageText != nil) {
                const char* utf8 = pageText.UTF8String;
                if (utf8 != nullptr) out.text += utf8;
                out.text += "\n";
            }
            ++out.pages;
        }
        if (out.text.size() > maxChars) {
            out.text.resize(maxChars);
            out.truncated = true;
        }
    }
    if (out.text.find_first_not_of(" \n\t\r") == std::string::npos) {
        out.error = "the PDF has no readable text (it may be a scanned image)";
        return out;
    }
    out.ok = true;
    return out;
}

}  // namespace easy::platform
