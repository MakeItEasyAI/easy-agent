#include "cli/cli.h"

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "actions/registry.h"
#include "cli/console_confirmer.h"
#include "core/planner.h"
#include "core/replay_engine.h"
#include "core/safety_guard.h"
#include "core/validator.h"
#include "models/gemini_provider.h"
#include "models/model_factory.h"
#include "platform/ui_automation.h"
#include "storage/config.h"
#include "storage/recipe_json.h"
#include "storage/storage.h"

namespace easy::cli {

namespace {

void printUsage() {
    std::cout << "easy - teach your computer a task once, run it forever\n\n"
                 "Usage:\n"
                 "  easy                            start a session: type tasks one after another\n"
                 "                                  until you type exit\n"
                 "  easy learn \"<task>\"            reuse a saved recipe for this task, or ask the\n"
                 "                                  AI for one, run it, and save it if it works\n"
                 "  easy run <name | file.json>     run a saved recipe or a recipe file (no AI)\n"
                 "  easy list                       list saved recipes\n"
                 "  easy show <name | file.json>    print the steps of a recipe\n"
                 "  easy plan \"<task>\"             only show what the AI would do (runs nothing)\n"
                 "  easy --version                  print version\n"
                 "\n"
                 "Options:\n"
                 "  --model <spec>                  AI to use. Default: EASY_MODEL, else \"model\" in\n"
                 "                                  config.json, else local Ollama.\n"
                 "                                    ollama[:<model>]   local, private (default\n"
                 "                                                       model qwen2.5:3b)\n"
                 "                                    gemini[:<model>]   Google, needs GEMINI_API_KEY\n"
                 "                                    gemini,ollama      try Gemini, use local if it\n"
                 "                                                       fails\n"
                 "  --config <config.json>          default: ~/.makeiteasy/config.json\n";
}

void printConfigHelp(const std::string& path) {
    std::cerr << "\nThe agent only touches folders you allow. Create " << path
              << " like this:\n\n"
                 "  {\n"
                 "    \"allowed_folders\": [\"~/Downloads\", \"~/Documents\"]\n"
                 "  }\n";
}

// One line per recipe for the session history, e.g.
//   open_url(url=https://www.linkedin.com/, app=Google Chrome)
std::string describeSteps(const core::Recipe& r) {
    std::string out;
    for (const auto& step : r.steps) {
        if (!out.empty()) out += "; ";
        out += step.action + "(";
        bool first = true;
        for (const auto& [key, value] : step.params) {
            std::string v = value;
            std::replace(v.begin(), v.end(), '\n', ' ');
            if (v.size() > 80) v = v.substr(0, 77) + "...";
            out += (first ? "" : ", ") + key + "=" + v;
            first = false;
        }
        out += ")";
    }
    return out;
}

void printRecipe(const core::Recipe& r) {
    std::cout << "Recipe: " << r.id << " (v" << r.version << ")\n";
    if (!r.task.empty()) std::cout << "Task:   " << r.task << "\n";
    std::cout << "Steps (" << r.steps.size() << "):\n";

    int n = 1;
    for (const auto& step : r.steps) {
        std::cout << "  " << n++ << ". " << step.action;
        for (const auto& [key, value] : step.params) {
            std::cout << "  " << key << "=" << value;
        }
        if (!step.save_as.empty()) std::cout << "  -> $" << step.save_as;
        std::cout << "\n";
    }
}

// Splits arguments into one positional value plus optional
//   --config <path>   and   --model <spec>
// Returns false (after printing why) on bad arguments.
bool parseArgs(int argc, char** argv, std::string& positional, std::string& configPath,
               std::string& modelSpec, int firstArg = 2) {
    configPath = storage::defaultConfigPath();
    for (int i = firstArg; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config" || arg == "--model") {
            if (i + 1 >= argc) {
                std::cerr << "Error: " << arg << " needs a value\n";
                return false;
            }
            (arg == "--config" ? configPath : modelSpec) = argv[++i];
        } else if (positional.empty()) {
            positional = arg;
        } else {
            std::cerr << "Error: unexpected argument: " << arg
                      << "\n(Put the task in quotes: easy learn \"move my PDFs\")\n";
            return false;
        }
    }
    return true;
}

// "move_bills" -> saved recipe; "path/to/file.json" -> that file.
std::optional<core::Recipe> loadRecipeArg(const std::string& arg) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(arg, ec)) {
        auto result = storage::loadRecipeFile(arg);
        if (!result.recipe) std::cerr << "Error: " << result.error << "\n";
        return result.recipe;
    }
    if (storage::isValidRecipeId(arg)) {
        storage::FileRecipeStore store(storage::defaultRecipesDir());
        if (auto recipe = store.load(arg)) return recipe;
    }
    std::cerr << "Error: no saved recipe or file named \"" << arg << "\" (see: easy list)\n";
    return std::nullopt;
}

// Splits "~/Downloads, ~/Documents" into folders (empty parts ignored).
std::vector<std::string> splitFolders(const std::string& text) {
    std::vector<std::string> folders;
    std::string current;
    for (char c : text + ",") {
        if (c == ',') {
            const auto first = current.find_first_not_of(' ');
            const auto last = current.find_last_not_of(' ');
            if (first != std::string::npos) folders.push_back(current.substr(first, last - first + 1));
            current.clear();
        } else {
            current += c;
        }
    }
    return folders;
}

// First run: no config yet -> ask which folders to allow and save them.
std::optional<storage::Config> setupConfig(const std::string& path, ConsoleConfirmer& ask) {
    const std::vector<std::string> defaults = {"~/Downloads", "~/Documents"};

    std::cout << "No allowed folders yet. The agent only touches folders you allow.\n";
    storage::Config config;
    if (ask.confirm("Allow these folders?  ~/Downloads, ~/Documents")) {
        config.allowed_folders = defaults;
    } else {
        const std::string typed = ask.askLine(
            "Type the folders to allow, separated by commas (Enter to cancel):\n>");
        config.allowed_folders = splitFolders(typed);
        if (config.allowed_folders.empty()) {
            std::cout << "Cancelled. Nothing was saved.\n";
            return std::nullopt;
        }
    }

    for (const std::string& folder : config.allowed_folders) {
        if (const std::string why = storage::tooBroadReason(folder); !why.empty()) {
            std::cerr << "Not allowed: " << folder << " (" << why
                      << "). Pick specific folders like ~/Downloads.\n";
            return std::nullopt;
        }
    }

    if (const std::string error = storage::saveConfigFile(path, config); !error.empty()) {
        std::cerr << "Error: " << error << "\n";
        return std::nullopt;
    }
    std::cout << "Saved to " << path << " (edit it any time to change folders).\n\n";
    return config;
}

// Loads the config; if it does not exist yet, offers to create it.
std::optional<storage::Config> loadConfigOrSetup(const std::string& path, ConsoleConfirmer& ask) {
    auto result = storage::loadConfigFile(path);
    if (result.config) return result.config;
    if (result.not_found) return setupConfig(path, ask);

    std::cerr << "Error: " << result.error << "\n";   // exists but broken: never overwrite it
    printConfigHelp(path);
    return std::nullopt;
}

// Which model to use: --model, else EASY_MODEL, else "model" in config, else local.
std::string chooseModelSpec(const std::string& cliSpec, const std::string& configSpec) {
    if (!cliSpec.empty()) return cliSpec;
    const char* env = std::getenv("EASY_MODEL");
    if (env != nullptr && *env != '\0') return env;
    return configSpec;   // may be "" -> makeModel uses the local default
}

std::unique_ptr<models::ModelProvider> makeModelOrExplain(const std::string& spec) {
    std::string error;
    auto model = models::makeModel(spec, error, std::cerr);
    if (!model) {
        std::cerr << "Error: " << error << "\n";
        if (error.find("GEMINI_API_KEY") != std::string::npos) {
            std::cerr << "\nGet a free key at https://aistudio.google.com, then in this terminal:\n"
                         "  export GEMINI_API_KEY=\"<your key>\"\n"
                         "Never put the key in a file inside the project.\n"
                         "Or use a local model instead:  --model ollama\n";
        }
    }
    return model;
}

// The model + planner for one command. Kept alive so a plan can be revised
// several times with the same model.
class AiSession {
public:
    static std::unique_ptr<AiSession> create(const std::string& modelSpec) {
        auto model = makeModelOrExplain(modelSpec);
        if (!model) return nullptr;
        return std::unique_ptr<AiSession>(new AiSession(std::move(model)));
    }

    std::optional<core::Recipe> plan(const std::string& task) {
        announce();
        return finish(planner_.plan(task), "Could not plan");
    }

    std::optional<core::Recipe> revise(const std::string& task, const core::Recipe& rejected,
                                       const std::string& feedback) {
        std::cout << "Asking the AI again with your note...\n";
        return finish(planner_.revise(task, rejected, feedback), "Could not change the plan");
    }

    std::optional<core::Recipe> planWithNote(const std::string& task, const std::string& note) {
        std::cout << "Asking the AI again with your note...\n";
        return finish(planner_.planWithNote(task, note), "Could not plan");
    }

    // True if the last failure was the model itself (network, key, Ollama
    // down...): a note from the user cannot fix that.
    bool lastFailureWasModel() const { return lastError_.rfind("model error", 0) == 0; }

    // What happened earlier in the session, sent with every prompt.
    void setContext(const std::string& context) { planner_.setContext(context); }

private:
    explicit AiSession(std::unique_ptr<models::ModelProvider> model)
        : model_(std::move(model)),
          registry_(actions::makeDefaultRegistry()),
          validator_(registry_),
          planner_(*model_, registry_, validator_) {}

    void announce() const {
        if (model_->isLocal()) {
            std::cout << "Asking " << model_->name() << " (local: nothing leaves this computer)...\n";
        } else {
            std::cout << "Asking " << model_->name()
                      << " (your task text may be sent over the internet)...\n";
        }
    }

    std::optional<core::Recipe> finish(core::PlanResult result, const char* what) {
        lastError_ = result.error;
        if (!result.recipe) {
            std::cout << std::flush;   // keep messages in order on screen
            std::cerr << what << ": " << result.error << std::endl;
            return std::nullopt;
        }
        return std::move(result.recipe);
    }

    std::unique_ptr<models::ModelProvider> model_;
    actions::ActionRegistry registry_;
    core::RecipeValidator validator_;
    core::LlmPlanner planner_;
    std::string lastError_;
};

// Runs a recipe through the safety guard + confirmations. Prints progress.
// Returns "" when it finished, otherwise why it stopped.
std::string runRecipe(core::Recipe& recipe, const storage::Config& config, core::Confirmer& confirmer) {
    const auto registry = actions::makeDefaultRegistry(config.allowed_apps);
    const core::FolderSafetyGuard guard(config.allowed_folders);
    core::SimpleReplayEngine engine(registry, guard, confirmer, std::cout);

    std::cout << "Running " << recipe.id << " (" << recipe.steps.size() << " steps)\n";
    const core::ReplayResult result = engine.replay(recipe);
    if (!result.ok) {
        std::cout << std::flush;
        std::cerr << "Stopped: " << result.message << std::endl;
        return result.message.empty() ? "stopped" : result.message;
    }
    std::cout << "Done: " << recipe.id << " (" << result.steps_done << " steps)\n";
    return "";
}

// ---------------- commands ----------------

// easy learn "<task>": saved recipe first, AI only if needed.
int learnTask(const std::string& task, const storage::Config& config, const std::string& modelSpec,
              ConsoleConfirmer& confirmer, std::unique_ptr<AiSession>& ai,
              const std::string& context = "", std::string* outcome = nullptr);

int learnCommand(int argc, char** argv) {
    std::string task, configPath, modelSpec;
    if (!parseArgs(argc, argv, task, configPath, modelSpec)) return 1;
    if (task.empty()) {
        std::cerr << "Usage: easy learn \"<task>\" [--model <spec>] [--config <config.json>]\n";
        return 1;
    }

    // Check config BEFORE calling the AI, so a missing config never wastes an AI call.
    ConsoleConfirmer confirmer;
    const auto config = loadConfigOrSetup(configPath, confirmer);
    if (!config) return 1;
    std::unique_ptr<AiSession> ai;
    return learnTask(task, *config, modelSpec, confirmer, ai);
}

// One task: saved recipe first, AI only if needed. `ai` is started on first
// use and kept, so a session can reuse the same (already loaded) model.
// context: what happened earlier in the session (for follow-up tasks).
// outcome: filled with a short summary of what happened (for the next task's context).
int learnTask(const std::string& task, const storage::Config& config, const std::string& modelSpec,
              ConsoleConfirmer& confirmer, std::unique_ptr<AiSession>& ai,
              const std::string& context, std::string* outcome) {
    storage::FileRecipeStore store(storage::defaultRecipesDir());
    auto report = [&](const std::string& what) {
        if (outcome) *outcome = what;
    };
    report("not done");

    // The AI is started only when it is needed (a new task, or a change).
    auto getAi = [&]() -> AiSession* {
        if (!ai) ai = AiSession::create(chooseModelSpec(modelSpec, config.model));
        if (ai) ai->setContext(context);
        return ai.get();
    };

    // 1. Already learned? Use the saved recipe (no AI). Otherwise ask the AI.
    std::optional<core::Recipe> recipe;
    std::string savedId;
    int savedVersion = 0;
    if (auto saved = store.findByTask(task)) {
        std::cout << "Found saved recipe \"" << saved->id << "\" for this task. No AI needed.\n";
        savedId = saved->id;
        savedVersion = saved->version;
        recipe = std::move(saved);
    } else {
        if (!getAi()) return 1;
        recipe = ai->plan(task);
    }
    bool changed = false;   // true once the user changed the plan

    // 2. Review loop. Ends only when the recipe ran fine or the user cancels
    //    (Enter with no text). Saying "n", or a step failing, asks what to change.
    constexpr int kMaxChanges = 5;
    std::string lastRunProblem;   // why the last run stopped (for the session history)
    for (int changes = 0;; ++changes) {
        std::string question;
        std::string lastProblem;
        if (recipe) {
            // New recipes are saved under the user's words, so the same request
            // matches next time. A follow-up ("now open it in the same window")
            // only makes sense with the session history, so then keep the AI's
            // self-contained description instead.
            if (savedId.empty() && (context.empty() || recipe->task.empty())) recipe->task = task;
            std::cout << "\n";
            printRecipe(*recipe);
            std::cout << "\n";
            if (confirmer.confirm("Run this recipe?")) {
                lastProblem = runRecipe(*recipe, config, confirmer);
                if (lastProblem.empty()) break;   // it worked
                lastRunProblem = lastProblem;
                question = "What should change? (Enter to stop)\n>";
            } else {
                question = "What should change? (Enter to cancel)\n>";
            }
        } else {
            if (ai && ai->lastFailureWasModel()) {   // a note cannot fix a model/network problem
                report("could not plan (AI not reachable)");
                return 1;
            }
            question = "Tell the AI more about what you want (Enter to cancel)\n>";
        }

        if (changes == kMaxChanges) {
            std::cout << "Stopped after " << kMaxChanges << " changes. Try rewording the task.\n";
            report("given up after several changes");
            return 1;
        }
        const std::string feedback = confirmer.askLine(question);
        if (feedback.empty()) {
            if (!savedId.empty()) std::cout << "Cancelled. The saved recipe was not changed.\n";
            else std::cout << "Cancelled. Nothing was saved.\n";
            report(lastRunProblem.empty() ? "cancelled by the user before running"
                                          : "stopped: " + lastRunProblem);
            return 1;
        }
        if (!getAi()) return 1;

        const std::string note =
            lastProblem.empty() ? feedback : feedback + " (the last run stopped with: " + lastProblem + ")";
        auto next = recipe ? ai->revise(task, *recipe, note) : ai->planWithNote(task, note);
        if (next) {
            if (!savedId.empty()) {   // fixing a saved recipe: keep its name and original task
                next->id = savedId;
                next->task = recipe ? recipe->task : task;
                next->version = savedVersion + 1;
            }
            recipe = std::move(next);
            changed = true;
        } else if (recipe) {
            std::cout << "Keeping the previous plan.\n";
        }
    }

    // 3. It worked. Save new recipes; update a saved one only if the user changed it.
    report("done: " + describeSteps(*recipe));
    if (!savedId.empty() && !changed) return 0;
    const bool updating = !savedId.empty();
    const std::string error = updating ? store.replace(*recipe) : store.save(*recipe);
    if (!error.empty()) {
        std::cerr << "Warning: task done, but recipe not saved: " << error << "\n";
        return 1;
    }
    if (updating) {
        std::cout << "\nUpdated saved recipe \"" << recipe->id << "\" (v" << recipe->version
                  << "). Next time it runs the fixed version, without AI.\n";
    } else {
        std::cout << "\nSaved as \"" << recipe->id << "\". Next time it runs instantly, without AI:\n"
                  << "  easy run " << recipe->id << "\n";
    }
    return 0;
}

// easy run <name | file.json>
int runCommand(int argc, char** argv) {
    std::string target, configPath, modelSpec;
    if (!parseArgs(argc, argv, target, configPath, modelSpec)) return 1;
    if (target.empty()) {
        std::cerr << "Usage: easy run <name | file.json> [--config <config.json>]\n";
        return 1;
    }

    auto recipe = loadRecipeArg(target);
    if (!recipe) return 1;
    ConsoleConfirmer confirmer;
    const auto config = loadConfigOrSetup(configPath, confirmer);
    if (!config) return 1;

    return runRecipe(*recipe, *config, confirmer).empty() ? 0 : 1;
}

// easy plan "<task>": show what the AI would do. Runs and saves nothing.
int planCommand(int argc, char** argv) {
    std::string task, configPath, modelSpec;
    if (!parseArgs(argc, argv, task, configPath, modelSpec)) return 1;
    if (task.empty()) {
        std::cerr << "Usage: easy plan \"<task>\" [--model <spec>]\n";
        return 1;
    }
    auto ai = AiSession::create(chooseModelSpec(modelSpec, [&] {
        const auto cfg = storage::loadConfigFile(configPath);   // optional for plan
        return cfg.config ? cfg.config->model : std::string();
    }()));
    if (!ai) return 1;
    auto recipe = ai->plan(task);
    if (!recipe) return 1;
    printRecipe(*recipe);
    std::cout << "\nNothing was run or saved. To run and save it: easy learn \"" << task << "\"\n";
    return 0;
}

// easy show <name | file.json>
int showCommand(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: easy show <name | file.json>\n";
        return 1;
    }
    auto recipe = loadRecipeArg(argv[2]);
    if (!recipe) return 1;
    printRecipe(*recipe);
    return 0;
}

// easy list
int listCommand() {
    storage::FileRecipeStore store(storage::defaultRecipesDir());
    const auto recipes = store.list();
    if (recipes.empty()) {
        std::cout << "No saved recipes yet. Teach one with: easy learn \"<task>\"\n";
        return 0;
    }
    std::cout << "Saved recipes (" << recipes.size() << ") in " << storage::defaultRecipesDir()
              << ":\n";
    for (const auto& r : recipes) {
        std::cout << "  " << r.id << "  (" << r.steps.size() << " steps)";
        if (!r.task.empty()) std::cout << "  " << r.task;
        std::cout << "\n";
    }
    return 0;
}

void printSessionHelp() {
    std::cout << "Type what you want done, in plain words, e.g.:\n"
                 "  open linkedin in google chrome\n"
                 "  move all PDFs from ~/Downloads/easy_test to ~/Documents/easy_bills\n"
                 "Other commands:\n"
                 "  list    show saved recipes\n"
                 "  forget  forget this session's earlier tasks (start fresh)\n"
                 "  help    show this help\n"
                 "  exit    leave (also: quit, bye, or Ctrl+D)\n"
                 "The AI remembers your last few tasks in this session, so you can say things\n"
                 "like \"now open notifications in the same window\".\n";
}

// easy  (no arguments): an interactive session. Keeps asking for the next task
// until you type exit. The AI model stays loaded between tasks.
int sessionCommand(const std::string& configPath, const std::string& modelSpec) {
    ConsoleConfirmer confirmer;
    const auto config = loadConfigOrSetup(configPath, confirmer);
    if (!config) return 1;

    std::unique_ptr<AiSession> ai;   // started on the first task that needs it
    std::deque<std::string> history; // what happened in this session (last few tasks)
    constexpr std::size_t kHistory = 5;
    const std::string spec = chooseModelSpec(modelSpec, config->model);
    std::cout << "easy agent ready (AI: " << (spec.empty() ? "ollama (local)" : spec)
              << "). Type a task, or: list, help, forget, exit\n";

    while (true) {
        const std::string line = confirmer.askLine("\n>");
        if (line.empty()) {
            if (confirmer.endOfInput()) break;   // Ctrl+D or no more input
            continue;                            // just Enter: ask again
        }
        const std::string word = storage::normalizeTask(line);
        if (word == "exit" || word == "quit" || word == "bye" || word == "q") break;
        if (word == "help" || word == "?") {
            printSessionHelp();
            continue;
        }
        if (word == "list") {
            listCommand();
            continue;
        }
        if (word == "forget") {
            history.clear();
            std::cout << "Forgot this session's earlier tasks.\n";
            continue;
        }

        std::string context;
        for (std::size_t i = 0; i < history.size(); ++i) {
            context += std::to_string(i + 1) + ". " + history[i] + "\n";
        }
        std::string outcome;
        learnTask(line, *config, modelSpec, confirmer, ai, context, &outcome);   // prints how it went

        history.push_back("task: \"" + line + "\" -> " + outcome);
        if (history.size() > kHistory) history.pop_front();
    }
    std::cout << "Bye!\n";
    return 0;
}

}  // namespace

int run(int argc, char** argv) {
    // No command (or only options like --model): start an interactive session.
    if (argc < 2 || std::string(argv[1]).rfind("--", 0) == 0) {
        const std::string first = argc >= 2 ? argv[1] : "";
        if (first != "--version" && first != "--help") {
            std::string unused, configPath, modelSpec;
            if (!parseArgs(argc, argv, unused, configPath, modelSpec, /*firstArg=*/1)) return 1;
            if (!unused.empty()) {
                std::cerr << "Unknown command: " << unused << "\n\n";
                printUsage();
                return 1;
            }
            return sessionCommand(configPath, modelSpec);
        }
    }

    const std::string cmd = argv[1];
    if (cmd == "--version" || cmd == "-v") {
        std::cout << "easy " << EASY_VERSION << "\n";
        return 0;
    }
    if (cmd == "--help" || cmd == "-h") {
        printUsage();
        return 0;
    }
    if (cmd == "learn") return learnCommand(argc, argv);
    if (cmd == "run") return runCommand(argc, argv);
    if (cmd == "list") return listCommand();
    if (cmd == "inspect") {
        if (argc < 3) {
            std::cerr << "Usage: easy inspect \"<app name>\"   e.g. easy inspect \"Google Chrome\"\n";
            return 1;
        }
        std::string error;
        const auto lines = platform::uiInspect(argv[2], error);
        if (!error.empty()) {
            std::cerr << "Error: " << error << "\n";
            return 1;
        }
        std::cout << "What easy can see in " << argv[2] << "'s front window:\n";
        for (const std::string& l : lines) std::cout << l << "\n";
        return 0;
    }
    if (cmd == "show") return showCommand(argc, argv);
    if (cmd == "plan") return planCommand(argc, argv);

    std::cerr << "Unknown command: " << cmd << "\n\n";
    printUsage();
    return 1;
}

}  // namespace easy::cli
