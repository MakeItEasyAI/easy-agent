#pragma once

// "Native messaging host" mode: how the Chrome extension talks to this agent.
// Chrome starts `easy native-host` and exchanges JSON messages over a private
// pipe (stdin/stdout) — no network port is opened. In this mode the program
// writes ONLY framed messages to stdout; everything else goes to stderr.
namespace easy::native {

int runNativeHost();

}  // namespace easy::native
