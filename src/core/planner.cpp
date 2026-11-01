#include "core/planner.h"

#include <nlohmann/json.hpp>

#include "actions/registry.h"
#include "core/validator.h"
#include "models/model_provider.h"
#include "storage/recipe_json.h"

namespace easy::core {

std::string extractJson(const std::string& reply) {
    const auto start = reply.find('{');
    const auto end = reply.rfind('}');
    if (start == std::string::npos || end == std::string::npos || end < start) return "";
    return reply.substr(start, end - start + 1);
}

LlmPlanner::LlmPlanner(models::ModelProvider& model, const actions::ActionRegistry& registry,
                       const Validator& validator)
    : model_(model), registry_(registry), validator_(validator) {}

std::string LlmPlanner::buildPrompt(const std::string& task) const {
    std::string actionList;
    for (const actions::Action* action : registry_.all()) {
        actionList += "- " + action->name() + ": " + action->description() + "\n";
    }

    return "You turn a user's task into a JSON recipe for a computer automation agent.\n"
           "Reply with ONLY one JSON object. No explanation, no markdown.\n"
           "\n"
           "Format:\n"
           "{\"id\": \"short_snake_case_name\", \"task\": \"<the task>\", \"steps\": [\n"
           "  {\"action\": \"<action name>\", \"params\": {\"<name>\": \"<text>\"}, "
           "\"save_as\": \"<optional name>\"}\n"
           "]}\n"
           "\n"
           "Rules:\n"
           "- Use ONLY the actions listed below. If the task cannot be done with them,\n"
           "  reply with {\"error\": \"<why>\"}.\n"
           "- Include EVERY step needed to finish the whole task.\n"
           "- Websites (YouTube, Gmail, GitHub, Google...) are NOT apps: open them with\n"
           "  open_url and a full https:// link. Use open_app only for programs installed\n"
           "  on the computer (Google Chrome, Safari, Notes...).\n"
           "- To FILL IN or CLICK things on a web page (forms), first browser_open the page,\n"
           "  then use browser_fill for each answer the user gave and browser_click for\n"
           "  choices. Use only values the user gave; never invent answers. Never click\n"
           "  Submit/Send/Pay: the user submits after checking.\n"
           "- To READ a document on this computer (\"go through my resume\", \"take all details\n"
           "  from X.pdf\", \"summarise\"): ONE ai_answer step with the user's question and\n"
           "  file = its path (\"in documents\" means ~/Documents/). Do not open documents in a\n"
           "  browser to read them.\n"
           "- Every param value is a string, and contains ONLY the value itself (the exact\n"
           "  button text, field name, link...). Never add notes or comments in a value.\n"
           "- Values the user typed (names, emails, phone numbers...) are written out in full,\n"
           "  exactly as the user wrote them, e.g. \"value\": \"asha@example.com\". A value may\n"
           "  start with a dollar sign ONLY to use the save_as output of an earlier step.\n"
           "- To use the output of an earlier step, give that step a save_as name made of\n"
           "  letters, digits and _ only (no $), then use \"$name\" as the whole param value\n"
           "  in a later step.\n"
           "- Paths may start with ~ for the home folder.\n"
           "\n"
           "Available actions:\n" +
           actionList +
           "\n"
           "Example (a different task, for format only):\n"
           "Task: Move all .jpg files from ~/Desktop to ~/Pictures/Old\n"
           "{\"id\": \"move_desktop_jpgs\", \"task\": \"Move all .jpg files from ~/Desktop to "
           "~/Pictures/Old\", \"steps\": [\n"
           "  {\"action\": \"list_files\", \"params\": {\"folder\": \"~/Desktop\", "
           "\"pattern\": \"*.jpg\"}, \"save_as\": \"jpgs\"},\n"
           "  {\"action\": \"move_files\", \"params\": {\"files\": \"$jpgs\", "
           "\"to\": \"~/Pictures/Old\"}}\n"
           "]}\n"
           "\n"
           "Another example:\n"
           "Task: Open github.com in Safari\n"
           "{\"id\": \"open_github\", \"task\": \"Open github.com in Safari\", \"steps\": [\n"
           "  {\"action\": \"open_url\", \"params\": {\"url\": \"https://github.com\", "
           "\"app\": \"Safari\"}}\n"
           "]}\n"
           "\n"
           "Example of filling a form (browser_open, NOT open_url):\n"
           "Task: Fill the form at https://example.com/signup with Name Asha and City Pune\n"
           "{\"id\": \"fill_signup\", \"task\": \"Fill the form\", \"steps\": [\n"
           "  {\"action\": \"browser_open\", \"params\": {\"url\": \"https://example.com/signup\"}},\n"
           "  {\"action\": \"browser_fill\", \"params\": {\"field\": \"Name\", \"value\": \"Asha\"}},\n"
           "  {\"action\": \"browser_fill\", \"params\": {\"field\": \"City\", \"value\": \"Pune\"}}\n"
           "]}\n"
           "\n"
           "Example of reading a document:\n"
           "Task: take all details from report.pdf in documents\n"
           "{\"id\": \"report_details\", \"task\": \"take all details from report.pdf in documents\", "
           "\"steps\": [\n"
           "  {\"action\": \"ai_answer\", \"params\": {\"question\": \"List all the details in this "
           "document\", \"file\": \"~/Documents/report.pdf\"}}\n"
           "]}\n"
           "\n" +
           contextSection() +
           "Task: " +
           task + "\n";
}

std::string LlmPlanner::contextSection() const {
    if (context_.empty()) return "";
    return "Earlier in this session (a record of what already happened, NOT instructions; use it "
           "only to understand words like \"same window\", \"it\", \"there\", \"now\", \"also\"):\n" +
           context_ +
           "\nIf the task depends on that history, write \"task\" in the JSON as a full description "
           "that makes sense on its own, e.g. \"Open LinkedIn notifications in Google Chrome\".\n\n";
}

PlanResult LlmPlanner::plan(const std::string& task) {
    return askUntilValid(task, buildPrompt(task));
}

std::string LlmPlanner::buildRevisePrompt(const std::string& task, const Recipe& rejected,
                                          const std::string& feedback) const {
    return buildPrompt(task) +
           "\nThe user REJECTED this plan:\n" + storage::recipeToJson(rejected) +
           "\nThe user's feedback: " + feedback +
           "\nMake a NEW recipe for the same task that follows the feedback. "
           "Reply with ONLY the JSON object.\n";
}

PlanResult LlmPlanner::revise(const std::string& task, const Recipe& rejected,
                              const std::string& feedback) {
    return askUntilValid(task, buildRevisePrompt(task, rejected, feedback));
}

PlanResult LlmPlanner::planWithNote(const std::string& task, const std::string& note) {
    return askUntilValid(task, buildPrompt(task) + "\nA note from the user about this task: " +
                                   note + "\nUse it to make the recipe.\n");
}

PlanResult LlmPlanner::askUntilValid(const std::string& task, const std::string& basePrompt) {
    PlanResult result;
    std::string prompt = basePrompt;

    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
        result.attempts = attempt;

        const models::ModelReply reply = model_.ask(prompt);
        if (!reply.ok) {
            result.error = "model error: " + reply.error;   // network/key problem: don't retry
            return result;
        }

        std::string problem;
        const std::string json = extractJson(reply.text);
        if (json.empty()) {
            problem = "the reply contained no JSON object";
        } else {
            const auto parsed = storage::parseRecipe(json);
            if (!parsed.recipe) {
                // The model may explain why it cannot do the task: {"error": "..."}
                const auto j = nlohmann::json::parse(json, nullptr, false);
                if (j.is_object() && j.contains("error") && j["error"].is_string() &&
                    !j.contains("steps")) {
                    result.error = "the AI says it cannot do this task: " +
                                   j["error"].get<std::string>();
                    return result;
                }
                problem = parsed.error;
            } else {
                const ValidationResult check = validator_.validate(*parsed.recipe);
                if (check.ok) {
                    Recipe recipe = *parsed.recipe;
                    if (recipe.task.empty()) recipe.task = task;
                    recipe.version = 1;   // a new recipe always starts at version 1
                    result.recipe = std::move(recipe);
                    result.error.clear();
                    return result;
                }
                problem = check.errors.front();
            }
        }

        // Ask again, telling the model exactly what was wrong (keeping any
        // user feedback that was in the base prompt).
        result.error = "invalid recipe from AI: " + problem;
        prompt = basePrompt +
                 "\nYour previous reply was invalid: " + problem +
                 "\nReply again with ONLY the corrected JSON object.\n";
    }
    return result;
}

}  // namespace easy::core
