#pragma once

#include "failable_minimal.h"
#include "types.h"

extern const csview
    terminal_link_begin_uri,
    terminal_link_uri_to_text,
    terminal_link_end_text;

void uri_encode_append(csview scheme, csview data, cstr* target);
failable existing_file_uri_encode_append(csview path, i64 line, cstr* target);
