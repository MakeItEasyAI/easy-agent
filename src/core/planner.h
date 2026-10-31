#pragma once

#include <optional>
#include <string>

#include "core/recipe.h"

namespace easy::actions {
class ActionRegistry;
}
namespace easy::models {
class ModelProvider;
}

// The ONLY part of the agent that talks to an AI model.
//   plan    - turn an English task into a recipe
//   revise  - make a new recipe after the user rejected one and said why
//   (later) fix - repair a single broken step during self-heal
namespace easy::core {

class Validator;

struct PlanResult {
    std::optional<Recipe> recipe;
    std::string error;
    int attempts = 0;   // how many times the model was asked
};

class Planner {
public:
    virtual ~Planner() = default;
    virtual PlanResult plan(const std::string& task) = 0;

    // The user rejected `rejected` and said what to change: make a new plan.
    virtual PlanResult revise(const std::string& task, const Recipe& rejected,
                              const std::string& feedback) = 0;

    // No usable plan yet (e.g. the AI gave up): plan again with the user's note.
    virtual PlanResult planWithNote(const std::string& task, const std::string& note) = 0;
};

// Asks the model for a JSON recipe, then checks it with parseRecipe + Validator.
// If the reply is bad, asks again once, telling the model what was wrong.
class LlmPlanner : public Planner {
public:
    static constexpr int kMaxAttempts = 3;

    LlmPlanner(models::ModelProvider& model, const actions::ActionRegistry& registry,
               const Validator& validator);

    PlanResult plan(const std::string& task) override;
    PlanResult revise(const std::string& task, const Recipe& rejected,
                      const std::string& feedback) override;
    PlanResult planWithNote(const std::string& task, const std::string& note) override;

    // Exposed for tests: the exact prompts sent to the model.
    std::string buildPrompt(const std::string& task) const;
    std::string buildRevisePrompt(const std::string& task, const Recipe& rejected,
                                  const std::string& feedback) const;

private:
    // Ask -> parse -> validate; on a bad reply ask again (up to kMaxAttempts).
    PlanResult askUntilValid(const std::string& task, const std::string& basePrompt);

    models::ModelProvider& model_;
    const actions::ActionRegistry& registry_;
    const Validator& validator_;
};

// Pulls the JSON object out of a model reply, removing ```json fences or
// extra text around it. Returns "" if there is no {...} in the reply.
std::string extractJson(const std::string& reply);

}  // namespace easy::core
