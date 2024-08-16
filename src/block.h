#pragma once

#include "prelude.h"
#include "type.h"

typedef struct Statement Statement;
typedef struct Type Type;
typedef struct ErrorCtx ErrorCtx;
typedef struct StatementCtx StatementCtx;
typedef struct NameStack NameStack;

wrap_forward_vec(Statement, statement);

typedef struct Block {
    VecStatement statements;
} Block;

void block_drop(Block* me);
void block_resolve_names_without_stack_frame(Block const* me, NameStack* names, ErrorCtx* errs);
void block_resolve_names(Block const* me, NameStack* names, ErrorCtx* errs);
void block_typecheck(Block* me, StatementCtx* ctx, ErrorCtx* errs);
void block_resolve_names_toplevel(Block* me, ErrorCtx* errs);
void block_typecheck_toplevel(Block* me, ErrorCtx* errs);
