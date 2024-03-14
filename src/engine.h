#pragma once

typedef struct Block Block;
typedef struct arc_Object arc_Object;

void eval_program(Block const* scope, arc_Object root);
