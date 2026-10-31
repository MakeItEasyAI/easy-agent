#include "models/http.h"

#include <curl/curl.h>

namespace easy::models {

namespace {

constexpr std::size_t kMaxResponseBytes = 10 * 1024 * 1024;   // 10 MB safety limit

// libcurl calls this for each chunk of the response.
std::size_t onData(char* data, std::size_t size, std::size_t count, void* userp) {
    auto* out = static_cast<std::string*>(userp);
    const std::size_t bytes = size * count;
    if (out->size() + bytes > kMaxResponseBytes) return 0;   // 0 = abort transfer
    out->append(data, bytes);
    return bytes;
}

// curl_global_init must run once per program.
struct CurlGlobal {
    CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobal() { curl_global_cleanup(); }
};

}  // namespace

bool httpAvailable() { return true; }

HttpResponse httpPostJson(const std::string& url, const std::vector<std::string>& headers,
                          const std::string& body) {
    static CurlGlobal global;
    HttpResponse response;

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        response.error = "could not start HTTPS client";
        return response;
    }

    curl_slist* headerList = nullptr;
    for (const std::string& h : headers) headerList = curl_slist_append(headerList, h.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);

    // Safety: HTTPS only, verify certificates (curl default, set explicitly),
    // never follow redirects, and never hang forever.
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 90L);

    const CURLcode code = curl_easy_perform(curl);
    if (code == CURLE_OK) {
        response.ok = true;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    } else {
        response.error = curl_easy_strerror(code);
    }

    curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);
    return response;
}

}  // namespace easy::models
