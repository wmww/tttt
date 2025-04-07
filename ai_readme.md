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
tttt uses the [STC](https://github.com/stclib/STC/tree/09790f024ad29fca6fe60528461eeb589d4a917b) library for containers in C, with some patches on top of it. This library uses some macro magic to support typesafe containers like the STL in C++. Refer to the documentation [here](https://raw.githubusercontent.com/stclib/STC/09790f024ad29fca6fe60528461eeb589d4a917b/README.md) for details.

A number of useful things are provided by `src/prelude.h`, including
- `#define my me->`: This allows writing `my foo.bar` instead of `me->foo.bar` in functions that take a pointer argument `me`. This is the preferred style for method-like functions (`me`/`my` is used instead of `this` or `self`)
- `PANIC("message %s %d", str, num);`: Immediately aborts the program with a message to stderr, if running under Valgrind a stack trace is printed to stderr as well
- `ASSERT(condition)`: Panics if condition is false, used for sanity checks
- `UNREACHABLE`: Panics if hit, used in places control flow should never hit
- `NONFATAL("message %s %d", str, num)`: Used for recoverable errors that do not require aborting, but should nonetheless never happen once the program is complete
- `PRINT_DBG("message %s %d", str, num)`: Can be used for print debugging

There's other stuff there, but those are the main things you need to be aware of. `prelude.h` is included directly or indirectly in all files.

## Project Structure
Feel free to change code in `src/` as much as you want, you can always revert it with git. Do not change code in `thirdparty/`, but you can look at it or anything you can find to understand what's going on. In particular `thirdparty/tsc/` may be enlightening, as this is what we're reimplementing.

Refer to `documentation.md` for a description of the overall architecture of the C code base in `src/`.

Good luck, and have fun!
