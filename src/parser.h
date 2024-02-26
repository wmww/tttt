#pragma once

#include "prelude.h"

typedef struct Scope Scope;

Scope parse_file(const char* src);
