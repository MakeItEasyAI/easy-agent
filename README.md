# easy-agent

> **MakeItEasy Agent** — teach your computer a task once, and it runs it forever.
> Fast, offline-first, single binary. No AI needed after learning.

⚠️ **Status:** v0.1 in progress. `easy show` and `easy run` work with hand-written recipes
and file actions (`list_files`, `move_files`). AI (`easy learn`) and scheduling are not built yet.

## Try it

```bash
# 1. Allow folders (the agent never touches anything else)
mkdir -p ~/.makeiteasy
cat > ~/.makeiteasy/config.json <<'EOF'
{ "allowed_folders": ["~/Downloads", "~/Documents"] }
EOF

# 2. Look at a recipe, then run it (asks before moving anything)
./build/easy show examples/recipes/move_bills.json
./build/easy run  examples/recipes/move_bills.json
```

## What it does

```bash
easy learn "Move PDFs from Downloads to Documents/Bills"   # AI plans it once
easy run move_bills                                        # replays, no AI
easy schedule move_bills "daily 18:00"                     # automate it
```

1. **Learn** — an AI (Gemini, Claude, or a local model) turns your request into steps.
2. **Save** — the steps are saved as a readable *recipe* after a successful run.
3. **Replay** — the recipe runs again instantly, for free, the same way every time.
4. **Self-heal** — if a step breaks, the AI fixes only that step (with your approval).

## Build

Requirements: CMake 3.20+, a C++20 compiler.

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
./build/easy --version
```

## Project layout

```
src/
  main.cpp        entry point
  cli/            command-line interface (learn / run / list / schedule)
  core/           recipe, validator, planner, replay engine, safety guard, scheduler
  models/         AI providers (Gemini, Claude, local llama.cpp)
  actions/        units of work (open_app, move_files, click, ...)
  platform/       OS-specific code (mac / windows / linux)
  storage/        recipes, schedules, logs on disk
tests/            unit tests
examples/         sample recipe and config
docs/             design documents
```

See [docs/design.md](docs/design.md) for the full design.

## Safety

- Every action passes through a safety guard: **Safe** (run), **Confirm** (ask you), **Blocked** (never).
- File actions only work inside folders you allow.
- Never types passwords. API keys are read from environment variables, never stored.

## License

MIT — see [LICENSE](LICENSE).
