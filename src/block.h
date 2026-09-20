#pragma once

#include "type.h"

typedef struct Statement Statement;
typedef struct TypeNode TypeNode;
typedef struct ErrorCtx ErrorCtx;

g_declare_vec(Statement, statement);

typedef struct BlockTypecheckCtx {
    TypeNode const* specified_return;
    bool break_allowed;
    bool continue_allowed;
    bool inside_label; // Labeled breaks are allowed
    bool ambient; // Inside a `declare namespace`/`declare module`, where every statement is implicitly declared
} BlockTypecheckCtx;

typedef struct Block {
    VecStatement statements;
} Block;

void block_drop(Block* me);
void block_add_names(Block const* me, Scope* scope);
// `export {}`, `export * from`, `export default x`: an ambient module with none of them exports everything
bool block_has_export_declarations(Block const* me);
void block_lookup_names(Block const* me, Scope* names, ErrorCtx* errs);
void block_resolve_names(Block const* me, Scope* names, ErrorCtx* errs);
// The types of the `return` expressions inside (nested functions excluded), checked with Checker.return_context
typedef struct TyBuf TyBuf;
void block_collect_return_types(Block const* me, TyBuf* out);
void block_typecheck(Block* me, BlockTypecheckCtx ctx, ErrorCtx* errs);
