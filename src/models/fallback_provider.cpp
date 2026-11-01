#include "models/fallback_provider.h"

#include <algorithm>
#include <ostream>

namespace easy::models {

FallbackProvider::FallbackProvider(std::vector<std::unique_ptr<ModelProvider>> providers,
                                   std::ostream& notices)
    : providers_(std::move(providers)), notices_(notices) {}

std::string FallbackProvider::name() const {
    std::string out;
    for (const auto& p : providers_) out += (out.empty() ? "" : " -> ") + p->name();
    return out;
}

bool FallbackProvider::isLocal() const {
    return std::all_of(providers_.begin(), providers_.end(),
                       [](const auto& p) { return p->isLocal(); });
}

ModelReply FallbackProvider::ask(const std::string& prompt) { return tryAll(prompt, false); }

ModelReply FallbackProvider::askText(const std::string& prompt) { return tryAll(prompt, true); }

ModelReply FallbackProvider::tryAll(const std::string& prompt, bool text) {
    std::string errors;
    // Start from the model that worked last time in this run, so a retry does
    // not wait for an already-failed model again.
    for (std::size_t i = start_; i < providers_.size(); ++i) {
        ModelProvider& model = *providers_[i];
        ModelReply reply = text ? model.askText(prompt) : model.ask(prompt);
        if (reply.ok) {
            start_ = i;
            return reply;
        }

        errors += (errors.empty() ? "" : "; ") + model.name() + ": " + reply.error;
        if (i + 1 < providers_.size()) {
            const ModelProvider& next = *providers_[i + 1];
            notices_ << "  " << model.name() << " failed (" << reply.error.substr(0, 120)
                     << ")\n  -> trying " << next.name()
                     << (next.isLocal() ? " (local)" : " (over the internet)") << "\n";
        }
    }
    return {false, "", "all models failed: " + errors};
}

}  // namespace easy::models
