#pragma once

#include "prelude.h"

typedef struct Block Block;
typedef struct ErrorCtx ErrorCtx;

Block parse_file(const char* source, ErrorCtx* errs);
