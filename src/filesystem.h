#pragma once

#include "prelude.h"

bool path_is_reg_file(const char* path);
bool path_is_directory(const char* path);

bool for_each_item_in_dir(
    const char* dir_path,
    void (*callback)(void* data, const char* dir_path, const char* name),
    void* data);

// Returns a pointer into the given string, returns nullptr if it has no extension
const char* file_extension_of(const char* path);
// Returns the final component of the given path, or "" if the path ends in a /
const char* basename_of(const char* path);
// Returns everything but the final component of the given path, includes the final /
csview dirname_of(const char* path);

// Allows for virtual filesystems for tests
typedef struct Filesystem Filesystem;
typedef struct Filesystem {
    bool (*canonicalize_path)(Filesystem const* me, const char* path, cstr* ret);
    bool (*read_entire_file)(Filesystem const* me, const char* path, cstr* ret);
    bool debug_on_fail;
} Filesystem;

// Returns true on success. Gets an absolute path without symlinks, like realpath()
static inline bool filesystem_canonicalize_path(Filesystem const* me, const char* path, cstr* ret) {
    return me->canonicalize_path(me, path, ret);
}
// Returns true on success
static inline bool filesystem_read_entire_file(Filesystem const* me, const char* path, cstr* ret) {
    return me->read_entire_file(me, path, ret);
}

extern const Filesystem real_filesystem_noisy;
extern const Filesystem real_filesystem_quiet;
