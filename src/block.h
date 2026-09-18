#pragma once

#include "type.h"

typedef struct Statement Statement;
typedef struct Type Type;
typedef struct ErrorCtx ErrorCtx;
typedef struct ScopeStack ScopeStack;

g_declare_vec(Statement, statement);

typedef struct BlockTypecheckCtx {
    Type const* specified_return;
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
void block_lookup_names(Block const* me, ScopeStack* names, ErrorCtx* errs);
void block_resolve_names(Block const* me, ScopeStack* names, ErrorCtx* errs);
Type block_evaluate_return_type(Block* me);
void block_typecheck(Block* me, BlockTypecheckCtx ctx, ErrorCtx* errs);
