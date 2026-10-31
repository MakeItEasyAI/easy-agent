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
           "- Every param value is a string.\n"
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
           "Task: " +
           task + "\n";
}

PlanResult LlmPlanner::plan(const std::string& task) {
    PlanResult result;
    std::string prompt = buildPrompt(task);

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

        // Ask again, telling the model exactly what was wrong.
        result.error = "invalid recipe from AI: " + problem;
        prompt = buildPrompt(task) +
                 "\nYour previous reply was invalid: " + problem +
                 "\nReply again with ONLY the corrected JSON object.\n";
    }
    return result;
}

}  // namespace easy::core
