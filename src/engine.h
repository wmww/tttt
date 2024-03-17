#pragma once

typedef struct Block Block;
typedef struct arc_js_object ArcObject;

void eval_program(Block const* scope, ArcObject root);
