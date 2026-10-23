#pragma once

#include "prelude.h"

typedef struct Program Program;

// `--expect-types FILE` (scripts/expecttypes.py, plan 44 stage 3 item 2): after checking, the type of each expression FILE
// names by its span (`path<TAB>line<TAB>col<TAB>endLine<TAB>endCol` lines, 1-based, columns in UTF-16 units like
// tsc's), printed to stdout as `path:line:col<TAB>type` the way DefinitelyTyped's `$ExpectType` lint rule prints it.
// Spans that aren't an expression print nothing
void program_print_expect_types(Program* program, char const* spans_path);

// `--node-shapes FILE` (scripts/projdiff.py --redact): for each position FILE lists (`path<TAB>line<TAB>col` lines,
// 1-based, UTF-16 columns), the syntactic shape there, printed as `path:line:col<TAB>shape`: the kinds of the nodes
// around it from its top-level statement down and the part of each it is in (`const > initializer > call > argument 1 >
// object literal > property value > arrow function`), words from a fixed vocabulary only, never source text. Needs
// the function bodies parsed (the CLI parses them up front with it) and nothing checked
void program_print_node_shapes(Program* program, char const* positions_path);
