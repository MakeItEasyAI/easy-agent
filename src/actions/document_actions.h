#pragma once

#include <functional>
#include <memory>
#include <string>

#include "actions/action.h"

namespace easy::models {
class ModelProvider;
}

// read_pdf / ai_answer: read a document and let the AI answer about it.
//   read_pdf   pull the text out of a PDF (no AI)        Safe; only inside allowed folders
//   ai_answer  the AI answers a question using ONLY      Safe with a local model;
//              the given text, shown to the user          Confirm with a cloud model
//                                                         (the text would leave the computer)
namespace easy::actions {

// Lazily creates the AI model when an ai_answer step needs it.
class AiAccess {
public:
    using Factory = std::function<std::unique_ptr<models::ModelProvider>(std::string& error)>;
    explicit AiAccess(Factory factory);
    ~AiAccess();
    models::ModelProvider* get(std::string& error);

private:
    Factory factory_;
    std::unique_ptr<models::ModelProvider> model_;
    std::string error_;
    bool tried_ = false;
};

// Exposed for tests: the prompt sent to the AI for an answer.
std::string buildAnswerPrompt(const std::string& question, const std::string& text);

class ReadPdfAction : public Action {
public:
    std::string name() const override { return "read_pdf"; }
    std::string description() const override {
        return "Read the text of a PDF file on this computer (no AI). params: path (required, "
               "e.g. \"~/Documents/resume.pdf\"). outputs: the text; give it a save_as name and "
               "pass it to ai_answer.";
    }
    core::RiskLevel risk() const override { return core::RiskLevel::Safe; }   // read-only
    std::string checkParams(const Params& params) const override;
    std::vector<std::string> pathParams() const override { return {"path"}; }
    ActionResult run(const Params& params) override;
    bool verify(const Params&) override { return true; }
};

class AiAnswerAction : public Action {
public:
    explicit AiAnswerAction(std::shared_ptr<AiAccess> ai) : ai_(std::move(ai)) {}
    std::string name() const override { return "ai_answer"; }
    std::string description() const override {
        return "Answer a question, summarise, or list details from a document, and show the answer "
               "to the user. params: question (required, e.g. \"List all details: name, email, "
               "phone, education, experience, skills\"), and EITHER file (a PDF path on this "
               "computer, e.g. \"~/Documents/resume.pdf\") OR text (usually \"$name\" from an "
               "earlier step).";
    }
    core::RiskLevel risk() const override { return core::RiskLevel::Confirm; }
    core::RiskLevel riskFor(const Params& params) const override;
    std::string checkParams(const Params& params) const override;
    std::vector<std::string> pathParams() const override { return {"file"}; }
    ActionResult run(const Params& params) override;
    bool verify(const Params&) override { return true; }

private:
    std::shared_ptr<AiAccess> ai_;
};

}  // namespace easy::actions
