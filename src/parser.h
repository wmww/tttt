#pragma once

#include "prelude.h"

typedef struct Scope Scope;

bool parse_file(const char* src, Scope* scope);
