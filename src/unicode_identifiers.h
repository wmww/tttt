#pragma once

#include "prelude.h"

// Whether a code point can start/continue an identifier (tsgo's IsUnicodeIdentifierStart/Part: Unicode's
// ID_Start/ID_Continue, whatever the target)
bool unicode_is_identifier_start(u32 code);
bool unicode_is_identifier_part(u32 code);
