# easy-agent design

## Goal

AI agents call the AI every time, even for the same task — slow, costly, unpredictable.
easy-agent lets the AI learn a task **once**, saves it as a **recipe**, and replays it
without AI. The AI is called again only when a step breaks.

## Non-goals (v1)

- General chat assistant
- Tasks that need fresh thinking every run
- CAPTCHAs, OTPs, logins

## Architecture

```
Interface (CLI)
   │
Core: Planner · Validator · Replay Engine · Safety Guard · Scheduler
   │
Models (Gemini / Claude / local) · Actions · Storage
   │
Platform (macOS / Windows / Linux)
```

The AI (Models) is used in only two places: **learn** and **fix**. Never in replay or schedule.

## Flows

**Learn:** task → Planner → LLM → Validator (bad → ask again) → show plan → approve?
(No → cancel) → Execute → success → **save recipe**.

**Run:** load recipe → Execute (safety guard per step → action → verify) → result + log.

**Schedule:** at set time → Run flow. No AI.

**Self-heal:** step fails → retry → LLM fix → Validator → ask user → Execute →
save as new version. If it still fails → stop and notify. Never loop forever.

## Safety

| Risk | Examples | Behaviour |
|---|---|---|
| Safe | list files, open app | run |
| Confirm | move/rename/delete, submit forms | ask user |
| Blocked | passwords, sudo, outside allowed folders | never run |

- File actions only inside allowed folders.
- Values passed directly to OS APIs, never built into shell strings.
- Web/file content is data, never instructions.
- API keys only from environment variables.

## Data

- Recipe: `examples/recipes/move_bills.json`
- Config: `examples/config.example.json`
- Runtime data: `~/.makeiteasy/` (recipes, schedules, logs)

## Version plan

| Version | Scope |
|---|---|
| v0.1 | Recipe format, replay engine, safety guard, file/app actions |
| v0.2 | Planner with Gemini (`easy learn`) |
| v0.3 | Click/type via Accessibility, action log |
| v0.4 | Scheduler |
| v0.5 | Self-heal |
| v0.6 | Local model (llama.cpp), Claude provider |
