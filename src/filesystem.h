#pragma once

#include "prelude.h"

bool path_is_reg_file(const char* path);
bool path_is_directory(const char* path);

bool for_each_item_in_dir(
    const char* dir_path,
    void (*callback)(void* data, const char* dir_path, const char* name),
    void* data);

// returns a pointer into the given string, returns nullptr if it has no extension
const char* file_extension_of(const char* path);
// returns the final component of the given path, or "" if the path ends in a /
const char* basename_of(const char* path);
// returns everything but the final component of the given path, includes the final /
csview dirname_of(const char* path);

// returns true on success, false on failure (optionally prints errors to stderr)
bool read_entire_file(const char* path, bool debug_on_fail, cstr* ret);
