#pragma once

#include "type.h"

typedef struct Statement Statement;
typedef struct TypeNode TypeNode;
typedef struct ErrorCtx ErrorCtx;

g_declare_vec(Statement, statement);

// The labels around a statement, innermost first (within the function)
typedef struct TypecheckLabel {
    Span name;
    bool is_iteration; // Labels a loop (maybe through more labels): `continue` can target it
    struct TypecheckLabel const* outer;
} TypecheckLabel;

typedef struct BlockTypecheckCtx {
    TypeNode const* specified_return;
    bool break_allowed;
    bool continue_allowed;
    bool inside_label; // Labeled breaks are allowed
    bool ambient; // Inside a `declare namespace`/`declare module`, where every statement is implicitly declared
    bool ambient_reported; // The block's statements can't be in an ambient context, and that's been said for them
    bool body; // The statement is the body of an `if` or a loop (through labels): no `let`, `const` or `using`
    bool in_function; // In a function body or class static block: a jump that finds no target crosses its boundary
    TypecheckLabel const* labels;
} BlockTypecheckCtx;

typedef struct Block {
    VecStatement statements;
} Block;

void block_drop(Block* me);
// The token that starts at `at` (tsc's getSpanOfTokenAtPosition, roughly), invalid if `at` is
Span block_first_token(Span at);
void block_add_names(Block const* me, Scope* scope);
// `export {}`, `export * from`, `export default x`: an ambient module with none of them exports everything
bool block_has_export_declarations(Block const* me);
// The resolve walk over the statements. is_block: a `{}` block or function body rather than the statements of a file
// or namespace (tsc reports unreachable code differently)
void block_lookup_names_as(Block const* me, bool is_block, Scope* names, ErrorCtx* errs);
// Same, as a control flow container of its own (flow.h): a file, a namespace body, a static block. `kind` and `name`
// are for TTTT_DUMP_FLOW
void block_lookup_names_container(Block const* me, bool is_block, csview kind, Span name, Scope* scope, ErrorCtx* errs);
void block_lookup_names_inline_container(Block const* me, Scope* scope, ErrorCtx* errs);
void block_resolve_names(Block const* me, Scope* names, ErrorCtx* errs);
// The types of the `return` expressions inside (nested functions excluded), checked with Checker.return_context
typedef struct TyBuf TyBuf;
void block_collect_return_types(Block const* me, TyBuf* out);
void block_typecheck(Block* me, BlockTypecheckCtx ctx, ErrorCtx* errs);
