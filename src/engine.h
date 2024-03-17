#pragma once

typedef struct Block Block;
typedef struct ArcObject ArcObject;

void eval_program(Block const* scope, ArcObject root);
