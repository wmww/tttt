#pragma once

#include "prelude.h"
#include "type.h"

typedef struct Statement Statement;
typedef struct Type Type;
typedef struct ErrorCtx ErrorCtx;

forward_vec(vec_Statement, Statement);

typedef struct Block {
    vec_Statement statements;
} Block;

void Block_drop(Block* self);
void cstr_append_block_debug(cstr* target, Block* self, int indent);
void Block_typecheck(Block* self, ErrorCtx* errs);
