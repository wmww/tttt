## Intro
This project, tttt, is a work-in-progress TypeScript reimplementation in C.

As an AI agent, your workflow will consist of running tests, understanding the differences between tttt and tsc, and making changes to tttt in order to make it pass more tests. You will not change the tests, as they come from tsc and thus always considered correct.

## Development Scripts
You will use scripts to help you run tests, but some scripts and options are more useful to AI agents than others. All scripts automatically compile the code in src before running tttt, and abort if there are any compile errors. The main scripts you will use are:

- `./fasttest.sh --summary`: Shows which new tests pass and which new tests fail
- `./test.sh -f specificTestName -d`: Shows the detailed output of a specific failing test, runs under valgrind, so may fail with a useful message if there are memory errors.
- `./test.sh -f specificTestName --sym`: Shows only symbols for a given test, flag can also be `--err` or `--typ`
- `./filetest path/to/file.ts -d`: Run a test on a file that is not part of TypeScript's existing test suite. This is useful for better understanding how both tttt and tsc work in a simplified case that may not otherwise be tested. You can create any typescript files you want and use this command on them
- `./run.sh path/to/file.ts`: Runs the typechecker as a user would. This produces type errors, but not the detailed information of the tests. It also doesn't run tsc against the file, so generally the test scripts are more useful

Tests are split into three test components, errors, symbols and types. A test component passes if tttt is able to annotate the input code the exact same ways as tsc. In the test output ` - ` marks a passing test component and `ERR`/`SYM`/`TYP` marks a failing one. In the diffs you get from failing tests, removed lines are lines tttt erroneously produced, and added lines are lines tttt failed to produce.

## Code Guidelines
tttt uses [cleat](https://codeberg.org/sphi/cleat) as its standard library, vendored at [thirdparty/cleat](thirdparty/cleat/). Cleat provides containers, strings, error handling, logging, filesystem access and subprocesses, and comes with its own vendored copy of the [STC](https://github.com/stclib/STC) container library. Cleat's own documentation is in [thirdparty/cleat/docs](thirdparty/cleat/docs/) and STC's is in [thirdparty/cleat/stc/docs](thirdparty/cleat/stc/docs/). The ones you'll want most often are:

- [generic.md](thirdparty/cleat/docs/generic.md): containers (`VecFoo`/`vec_foo_*`, `HMapFoo`/`hmap_foo_*`, `ArcFoo`, `BoxFoo`, ...). A container is instantiated by `#define`ing `g_key`/`g_class_key`/`g_pro_key` (and `g_val`/`g_class_val`/`g_pro_val` for maps) and then `#include <cleat/generic/vec.h>`
- [error_handling.md](thirdparty/cleat/docs/error_handling.md): `failable` functions, `try (...) bubble`, `ok`/`err`, `returns`
- [display.md](thirdparty/cleat/docs/display.md): format-string-free printing and formatting (`println()`, `cstr_d()`, `err_d()`, `DISPLAYABLES`)
- [filesystem.md](thirdparty/cleat/docs/filesystem.md): the `Fs` interface and the `fs` global
- [iterators_and_for_each.md](thirdparty/cleat/docs/iterators_and_for_each.md): `for_each`/`for_each_kv`

Cleat wraps every STC knob we need as a `g_*` macro, so raw `i_*` macros shouldn't appear in `src/`. If you hit one that isn't wrapped, prefer adding it to cleat over reaching past the wrapper.

Note that `g_pro_key CStr, cstr` keys a container by `cstr` but looks it up by `const char*`, which is no good for most of our strings: they're `csview`s into a source file and so aren't null terminated. Use `g_pro_key SvStr, svstr` instead, which is the same `cstr` storage with `csview` as the lookup type.

`src/prelude.h` pulls in `cleat.h` and is included directly or indirectly in all files. Between them you get, among other things:
- `#define my me->` (from cleat): This allows writing `my foo.bar` instead of `me->foo.bar` in functions that take a pointer argument `me`. This is the preferred style for method-like functions (`me`/`my` is used instead of `this` or `self`)
- `panic("message ", str, " ", num);` (from cleat): Immediately aborts the program with a message to stderr, if running under Valgrind a stack trace is printed to stderr as well, and the source location is a clickable link. `panic_if(condition, ...)` is the conditional form
- `check(condition)` (from cleat): Panics if condition is false, used for sanity checks
- `impossible` (from cleat): Panics if hit, used in places control flow should never hit
- `log_error("message ", str, " ", num)` (from cleat): Used for recoverable errors that do not require aborting, but should nonetheless never happen once the program is complete. `log_warn()`/`log_info()`/`log_debug()` are also available for print debugging, but note that `logger_config.threshold` hides anything below info by default

All of these take a [display](thirdparty/cleat/docs/display.md) sequence rather than a format string, and `src/display.h` adds tttt's own types (`Span`, `Type const*`, `Expr const*`, `Error const*`, ...) to `DISPLAYABLES`, so AST nodes can be passed straight in. `debug_d(x)` switches an AST node from its error-message form to its developer form.

There's other stuff there, but those are the main things you need to be aware of.

## Project Structure
Feel free to change code in `src/` as much as you want, you can always revert it with git. Do not change code in `thirdparty/`, but you can look at it or anything you can find to understand what's going on; if a vendored library genuinely needs a fix, add a `thirdparty/*patch.txt` alongside the existing ones so `thirdparty/refresh.sh` reapplies it after a re-download. In particular `thirdparty/tsc/` may be enlightening, as this is what we're reimplementing.

Refer to `documentation.md` for a description of the overall architecture of the C code base in `src/`.

Good luck, and have fun!
