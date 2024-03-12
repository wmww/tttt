#pragma once

#include "prelude.h"
#include "type.h"

typedef struct Statement Statement;
typedef struct Type Type;
typedef struct ErrorCtx ErrorCtx;
typedef struct StatementCtx StatementCtx;
typedef struct NameStack NameStack;

forward_vec(vec_Statement, Statement);

typedef struct Block {
    vec_Statement statements;
} Block;

void Block_drop(Block* self);
void cstr_append_block_debug(cstr* target, Block* self, int indent);
void Block_resolve_names_without_stack_frame(Block* self, NameStack* names, ErrorCtx* errs);
void Block_resolve_names(Block* self, NameStack* names, ErrorCtx* errs);
void Block_typecheck(Block* self, StatementCtx* ctx, ErrorCtx* errs);
void Block_resolve_names_toplevel(Block* self, ErrorCtx* errs);
void Block_typecheck_toplevel(Block* self, ErrorCtx* errs);
