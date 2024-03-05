#pragma once

#include "prelude.h"

typedef struct Block Block;
typedef struct ErrorCtx ErrorCtx;

Block parse_file(const char* src, ErrorCtx* errs);
