#pragma once

#include "prelude.h"

typedef struct Program Program;

// `--expect-types FILE` (scripts/expecttypes.py, plan 44 stage 3 item 2): after checking, the type of each expression FILE
// names by its span (`path<TAB>line<TAB>col<TAB>endLine<TAB>endCol` lines, 1-based, columns in UTF-16 units like
// tsc's), printed to stdout as `path:line:col<TAB>type` the way DefinitelyTyped's `$ExpectType` lint rule prints it.
// Spans that aren't an expression print nothing
void program_print_expect_types(Program* program, char const* spans_path);
