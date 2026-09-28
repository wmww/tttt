#pragma once

#include "prelude.h"

// Whether a code point can start/continue an identifier (tsc's isUnicodeIdentifierStart/Part): the ES5 tables, or
// with `esnext` (target ES2015 and later) the current Unicode ID_Start/ID_Continue ones
bool unicode_is_identifier_start(u32 code, bool esnext);
bool unicode_is_identifier_part(u32 code, bool esnext);
