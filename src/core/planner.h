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
//   v0.2: plan  - turn an English task into a recipe
//   v0.5: fix   - repair a single broken step during self-heal (later)
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
};

// Asks the model for a JSON recipe, then checks it with parseRecipe + Validator.
// If the reply is bad, asks again once, telling the model what was wrong.
class LlmPlanner : public Planner {
public:
    static constexpr int kMaxAttempts = 2;

    LlmPlanner(models::ModelProvider& model, const actions::ActionRegistry& registry,
               const Validator& validator);

    PlanResult plan(const std::string& task) override;

    // Exposed for tests: the exact prompt sent to the model.
    std::string buildPrompt(const std::string& task) const;

private:
    models::ModelProvider& model_;
    const actions::ActionRegistry& registry_;
    const Validator& validator_;
};

// Pulls the JSON object out of a model reply, removing ```json fences or
// extra text around it. Returns "" if there is no {...} in the reply.
std::string extractJson(const std::string& reply);

}  // namespace easy::core
