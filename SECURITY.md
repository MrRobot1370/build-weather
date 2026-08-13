# Security policy

## Supported versions

Build Weather is developed on the default branch, and fixes go there. There
are no maintained release branches, so "supported" means the latest commit.

## Reporting a vulnerability

Please report privately rather than in a public issue. Use GitHub's
[private vulnerability reporting](https://github.com/MrRobot1370/build-weather/security/advisories/new)
on this repository.

Expect an acknowledgement within a week. If a report is valid, the fix and a
note in `CHANGELOG.md` follow, and you will be credited unless you would
rather not be.

## What is actually in scope

Build Weather is a desktop tool with no network surface: it opens no sockets,
downloads nothing, and has no server component. Realistically the interesting
surface is that **it parses untrusted files**, so the things worth reporting
are:

- A `.ninja_log`, `compile_commands.json` or `-ftime-trace` document that
  causes a crash, a hang, unbounded memory growth, or an out-of-bounds read in
  `libs/BW_Build` or `libs/BW_Core`. These parsers are hand-written and take
  arbitrary input; a fuzz-found crash is a legitimate report.
- A path in one of those files that escapes where it should stay, for example
  causing a write outside a chosen export destination.
- Anything that turns opening a build directory into code execution.

Out of scope: the fact that pressing **Build** runs `ninja`, which then runs
your compiler. That is the entire purpose of the feature, and the build
directory you point it at is already trusted by definition.
