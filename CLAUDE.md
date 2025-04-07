# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build/test Commands
- `./run.sh file.ts` - Debug build with Valgrind
- `./test.sh -f testName -d` - Run specific test name from tsc with diff
- `./fasttest.sh --summary` - Run all tests with optimizations
- `./filetest.sh path/to/file.ts -d` - Test arbitrary file with diff

## Code Style Guidelines
- Use struct-oriented programming with `my foo` for member access (aliased to `me->` by macros)
- Function naming: `[module]_[action]` (e.g., `type_drop`, `error_ctx_init`)
- Memory management: Always call appropriate `*_drop` functions for cleanup
- Error handling: Use `ASSERT`, `PANIC_IF`, `NONFATAL` macros appropriately
- Parser functions return bool: `parse_success(&p)` (true) or `parse_fail(&p)` (false)
- Include sufficient context in `Edit` tool calls (3-5 lines before/after)
- Don't modify AST structure after parsing (pointers need to remain valid)
- Follow existing conventions in related files for imports and code organization
