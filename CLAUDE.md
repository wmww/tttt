# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build/test Commands
- `./run.sh file.ts` - Debug build with Valgrind
- `./test.sh -f testName -d` - Run specific test name from tsc with diff
- `./fasttest.sh --summary` - Run all tests with optimizations
- `./filetest.sh path/to/file.ts -d` - Test arbitrary file with diff

## Code Style Guidelines
- [cleat](https://codeberg.org/sphi/cleat) (vendored at `thirdparty/cleat/`) is the standard library: containers, strings, error handling, logging, filesystem, subprocesses. Its docs are in `thirdparty/cleat/docs/`, STC's in `thirdparty/cleat/stc/docs/`. Never edit `thirdparty/`; if a vendored fix is needed, add a `*patch.txt` next to the others and `thirdparty/refresh.sh` will reapply it
- Containers are instantiated with cleat's `g_*` macros (`g_key`, `g_class_key`, `g_pro_key`, `g_val`, ...) followed by `#include <cleat/generic/vec.h>`. Don't use raw STC `i_*` macros. For a `cstr` key looked up by `csview` rather than `const char*`, use `g_pro_key SvStr, svstr`
- Use struct-oriented programming with `my foo` for member access (aliased to `me->` by macros)
- Function naming: `[module]_[action]` (e.g., `type_drop`, `error_ctx_init`)
- Memory management: Always call appropriate `*_drop` functions for cleanup
- Error handling: Use cleat's `check()`, `panic()`/`panic_if()`, `impossible` and `log_error()` appropriately. They take display sequences, not format strings
- Parser functions return bool: `parse_success(&p)` (true) or `parse_fail(&p)` (false)
- Include sufficient context in `Edit` tool calls (3-5 lines before/after)
- Don't modify AST structure after parsing (pointers need to remain valid)
- Follow existing conventions in related files for imports and code organization
