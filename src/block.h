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
    struct Symbol const* constructed_class; // In a constructor: what a returned value has to be an instance of
    // In an async function or generator with an annotation: what returned values are checked against instead of
    // specified_return (tsc's unwrapReturnType), and whether they're awaited first. Null: not checked
    struct Ty const* unwrapped_return;
    bool await_returned;
    struct FunctionImpl const* function; // The function whose body it is in (null in a static block or at the top)
    bool in_setter; // Returned values are TS2408
    bool break_allowed;
    bool continue_allowed;
    bool inside_label; // Labeled breaks are allowed
    bool ambient; // Inside a `declare namespace`/`declare module`, where every statement is implicitly declared
    bool ambient_reported; // The block's statements can't be in an ambient context, and that's been said for them
    void const* ambient_error_statement; // The statement TS1036 was reported on, which gets no other grammar error
    bool case_clause; // The statement is directly in a `case` or `default` clause
    bool body; // The statement is the body of an `if` or a loop (through labels): no `let`, `const` or `using`
    bool in_function; // In a function body or class static block: a jump that finds no target crosses its boundary
    TypecheckLabel const* labels;
} BlockTypecheckCtx;

typedef struct Block {
    VecStatement statements;
    // The scope of a function body or block whose declarations the resolve walk added (block_add_names()), null for
    // a file's or namespace's statements and blocks that declare nothing: where the types baseline names types from
    // A FunctionImpl's whose body is deferred (FunctionImpl.deferred, plan 78): the DeferredBody instead
    union {
        struct Scope* scope;
        struct DeferredBody* deferred;
    };
} Block;

void block_drop(Block* me);
// The token that starts at `at` (tsc's getSpanOfTokenAtPosition, roughly), invalid if `at` is
Span block_first_token(Span at);
void block_check_required_declare(Block const* me, ErrorCtx* errs); // In a declaration file: TS1046
struct SourceFile;
// verbatimModuleSyntax: TS1287 on the top-level `export` of a value declaration in a file emitted as CommonJS (statement.c)
void source_file_check_verbatim_exports(struct SourceFile const* file, ErrorCtx* errs);
Span block_span(Block const* me); // From its first statement to its last, invalid if it has none
void block_add_names(Block const* me, Scope* scope);
// `export {}`, `export * from`, `export default x`: an ambient module with none of them exports everything
bool block_has_export_declarations(Block const* me);
// The resolve walk over the statements
void block_lookup_names_as(Block const* me, Scope* names, ErrorCtx* errs);
/// A statement an ambient context doesn't allow (tsc's checkGrammarStatementInAmbientContext: TS1036)
bool block_statement_is_ambient_disallowed(Statement const* me);
// Same, as a control flow container of its own (flow.h): a file, a namespace body, a static block. `kind` and `name`
// are for TTTT_DUMP_FLOW
void block_lookup_names_container(Block const* me, csview kind, Span name, Scope* scope, ErrorCtx* errs);
struct FlowNode const* block_lookup_names_inline_container(Block const* me, Scope* scope, ErrorCtx* errs); // Returns its end
void block_resolve_names(Block const* me, Scope* names, ErrorCtx* errs);
// The types of the `return` expressions inside (nested functions excluded), checked with Checker.return_context
typedef struct TyBuf TyBuf;
void block_collect_return_types(Block const* me, TyBuf* out);
void block_typecheck(Block* me, BlockTypecheckCtx ctx, ErrorCtx* errs);
