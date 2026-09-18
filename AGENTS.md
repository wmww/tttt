# tttt
tttt is a work-in-progress TypeScript reimplementation in C.

## Objective
The project's focus is performance, ideally beating the new typescript-go. But we need to fully support the TypeScript language first. That's what you're working on today.

Tests are the oracle, but not the true end goal. Special casing tests, or getting a few more tests to pass under an architecture that is fundamentally flawed doesn't help anyone. Keep the big picture in mind.

This is a massive project and I'm giving you and all the other agent sessions a lot of autonomy. Use it wisely.

## Agents directory
Keep notes for future agent sessions in `agents/`, including:
- `agents/vision.md` - high-level overview of the state of the project, concerns, risks and where you plan to take it.
- `agents/todo.md` - brief summary of the next few sessions, churns often, references plans rather than containing detailed per-session instructions.
- `agents/hacks.md` - contains a comprehensive list of hacks, workarounds and other suboptimal-but-working aspects of the code. Prefer function names over line numbers for stability. Add when writing a hack, or when discovering code that seems sus. Hacks don't need to be immediately addressed, they're meant for discovering and fixing antipatterns.
- `agents/triage.md` - a dumping ground for uninvestigated bugs, potential long-term problems, papercuts, ideas, and anything else an agent doesn't want to deal with at the moment, but a future agent should sort out.
- `agents/bugs/*.md` - reports of issues and problems without an active plan to solve them yet.
- `agents/plans/*.md` - detailed plans for future work.

`agents/` should be committed to git just like code. Create, edit and delete files in `agents/` freely. Remove files that are no longer relevant (eg solved bugs, completed plans) and clean up confusing and unimportant references to previous state.

## Session types
You are to work on this project highly autonomously over many sessions. To facilitate that, you are one of the following types of agents:

### Insights
You are a high level manager. Don't get bogged down in the details of any one problem or refactor. Review vision.md, todo.md, hacks.md and the git log, and keep the project on track. Be on the lookout for patterns spanning many past sessions. Is a particular area of the code base repeatedly more troublesome than expected? Are we accumulating tech debt? Does something *almost* work in a way that's tempting to keep charging forwards, but actually demands a significant refactor? Is the scope or focus of the project drifting from the true goal? These are the sorts of things you need to be thinking about and correcting for. Don't be afraid to make bold decisions.

Delegate to sub-agents or future sessions as needed. For example, instead of doing your own refactor, or even doing a detailed code investigation to write a refactor plan, you could instead write a simple plan that instructs another agent to investigate, make a decision, and write up the real refactor plan if needed. You might spend a lot of time in one session to get things right, or exit quickly if everything's going well. Your main deliverable is maintaining vision.md to the highest quality, and writing todo.md to map out the next 2-10 sessions (before a the next insights session). Always finish todo.md with a triage session.

### Planning
You are to research, derisk and write up the specified plan(s). Be reasonably detailed, but leave things up to the execution agent if they truly depend on information we don't have yet. Include how to validate that the plan has been completed.

### Execution
You're given a plan file (or in some cases just a specified task) and you execute it. Deliverable might be a new feature, a perf improvement, a measurement, a dev tool or a refactor.

### Triage
You're in charge of draining triage.md and otherwise maintaining the repo. Validate and write up bugs, research and write plans, update todo.md based on new information, delete or consolidate old files, etc.

## Workflow
- Commit at the end of every session. Commit messages should be a summary of what you did and what happened, generally a paragraph or two. Include doubts and things that went wrong, these will be helpful later.
- Don't use feature branches. If you change branch, always change back by end of session.
- There is no strict chain of command or scoping of agents. An insights agent may write some code, an execution agent may edit vision.md. The structure is just to keep everyone focused on what's usually most useful to them.
- Any agent may "fail", if that's best for the project. If half way through implementing a plan you discover the plan takes the wrong approach, cancel your implementation and report that rather than pushing through.
- Feel free to build tools and scripts, and share them with future agents if they're generally useful. The workflow of this project was originally optimized for humans, so the tooling might need adapting to be optimal for agents.
- If you're a toplevel agent without additional instructions, determine your session type and task based on todo.md. If it's empty/complete, congrats! You're an insights agent.
- Feel free to delegate easy/mechanical/repetitive work to a smaller model sub-agent
- Be conservative with sub-agents, especially those of the most capable model. Don't fan out to many of them.

# AGENTS MAY EDIT THIS FILE BELOW THIS LINE
(keep it brief and focused on info that's actually critical for every agent to know)

## Development Scripts
You will use scripts to help you run tests, but some scripts and options are more useful to AI agents than others. All scripts automatically compile the code in src before running tttt, and abort if there are any compile errors. The main scripts you will use are:

- `./fasttest.sh --summary`: Shows which new tests pass and which new tests fail, compared to the saved results (`test/baseline_results.ts`). **Save them with `./fasttest.sh -s` before you commit** (`./passcount.py` prints the totals of the saved file), or the next session's summary is polluted by yours
- `./test.sh -f specificTestName -d`: Shows the detailed output of a specific failing test, runs under valgrind, so may fail with a useful message if there are memory errors.
- `./test.sh -f specificTestName --sym`: Shows only symbols for a given test, flag can also be `--err` or `--typ`
- `./filetest.sh path/to/file.ts -d`: Run a test on a file that is not part of TypeScript's existing test suite. This is useful for better understanding how both tttt and tsc work in a simplified case that may not otherwise be tested. You can create any typescript files you want and use this command on them. Prefix with `FAST=1` to use the release build without valgrind (seconds instead of a minute)
- A test file with `// @target: es5,es2015` (any boolean/enum option with several values) is one test per configuration, named `name(target=es5)` like tsc's baselines. `-f name` runs all configurations of it. Compiler options live in `CompilerOptions` (`src/compiler_options.h`), read through `compiler_options()`
- `-f` takes a regex that must match the whole test name (`-f '^async.*'`, not `-f async`). If a full run gets OOM-killed or hangs, some newly reachable checker path is running away: bisect with `(ulimit -v 6000000; ./release/baseline_tests -f '^a.*')` per letter
- `./failstats.py`: Runs the whole suite (~10s) and reports *why* tests fail in aggregate: checker `log_error`s, missing/spurious diagnostic codes, nearly-passing tests. `--syntax` lists the syntax shapes the parser wrongly rejects, `--code TS2322` lists the tests affected by a code, `-f regex` restricts it. Use it to pick work that moves whole categories. Latest numbers are in `agents/measurements.md`
- `./findcrash.py [prefix]`: When the release suite dies (crash, hang, memory runaway: it runs each probe under `ulimit -v` and `timeout`), bisects on test name prefixes and prints the tests responsible
- `TTTT_DEBUG_RELATE=1` logs every top-level type relation query (source, target, ids, result), handy with `./run.sh` or `./release/tttt` on a small file
- `./bench.sh`: Perf benchmark against tsc and tsgo (wall time, peak RSS, tttt phase split). Rerun and update `agents/measurements.md` after perf-relevant changes
- `./run.sh path/to/file.ts`: Runs the typechecker as a user would. This produces type errors, but not the detailed information of the tests. It also doesn't run tsc against the file, so generally the test scripts are more useful

Tests are split into three test components, errors, symbols and types. A test component passes if tttt is able to annotate the input code the exact same ways as tsc. In the test output ` - ` marks a passing test component and `ERR`/`SYM`/`TYP` marks a failing one. In the diffs you get from failing tests, removed lines are lines tttt erroneously produced, and added lines are lines tttt failed to produce.

## Code Guidelines
tttt uses [cleat](https://codeberg.org/sphi/cleat) as its standard library, vendored at [thirdparty/cleat](thirdparty/cleat/). Cleat provides containers, strings, error handling, logging, filesystem access and subprocesses, and comes with its own vendored copy of the [STC](https://github.com/stclib/STC) container library. Cleat's own documentation is in [thirdparty/cleat/docs](thirdparty/cleat/docs/) and STC's is in [thirdparty/cleat/stc/docs](thirdparty/cleat/stc/docs/).

`src/prelude.h` pulls in `cleat.h` and is included directly or indirectly in all files. Between them you get, among other things:
- `#define my me->` (from cleat): This allows writing `my foo.bar` instead of `me->foo.bar` in functions that take a pointer argument `me`. This is the preferred style for method-like functions (`me`/`my` is used instead of `this` or `self`)
- `panic("message ", str, " ", num);` (from cleat): Immediately aborts the program with a message to stderr, if running under Valgrind a stack trace is printed to stderr as well, and the source location is a clickable link. `panic_if(condition, ...)` is the conditional form
- `check(condition)` (from cleat): Panics if condition is false, used for sanity checks
- `impossible` (from cleat): Panics if hit, used in places control flow should never hit
- `log_error("message ", str, " ", num)` (from cleat): Used for recoverable errors that do not require aborting, but should nonetheless never happen once the program is complete. `log_warn()`/`log_info()`/`log_debug()` are also available for print debugging, but note that `logger_config.threshold` hides anything below info by default

All of these take a [display](thirdparty/cleat/docs/display.md) sequence rather than a format string.

The project was originally a cleanroom implementation (I didn't read tsc other than tests). that rule is no longer in play but I still recommend paying more attention to what tsc observably does than how it does it.

## Project Structure
Feel free to change code in `src/` as much as you want, you can always revert it with git. Do not change code in `thirdparty/`, but you can look at it or anything you can find to understand what's going on; if a vendored library genuinely needs a fix, add a `thirdparty/*patch.txt` alongside the existing ones so `thirdparty/refresh.sh` reapplies it after a re-download. In particular `thirdparty/tsc/` may be enlightening, as this is what we're reimplementing.

Refer to `documentation.md` for a description of the overall architecture of the C code base in `src/`.

Good luck, and have fun!
