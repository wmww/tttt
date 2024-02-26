#pragma once

#include "prelude.h"

typedef struct Scope Scope;
typedef struct ErrorCtx ErrorCtx;

Scope parse_file(const char* src, ErrorCtx* errs);
