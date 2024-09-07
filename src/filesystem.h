#pragma once

#include "prelude.h"

// Returns a pointer into the given string, returns nullptr if it has no extension
const char* file_extension_of(const char* path);
// Returns the final component of the given path, or "" if the path ends in a /
const char* basename_of(const char* path);
// Returns everything but the final component of the given path, includes the final /
csview dirname_of(const char* path);

// Allows for virtual filesystems for tests
typedef struct Fs Fs;
typedef struct Fs {
    bool (*is_reg_file)(Fs const* me, const char* path);
    bool (*is_directory)(Fs const* me, const char* path);
    bool (*for_each_item_in_dir)(
        Fs const* me,
        const char* dir_path,
        void (*callback)(void* data, const char* dir_path, const char* name),
        void* data);
    bool (*canonicalize_path)(Fs const* me, const char* path, cstr* ret);
    bool (*read_entire_file)(Fs const* me, const char* path, cstr* ret);
    bool debug_on_fail;
} Fs;

static inline bool fs_is_reg_file(Fs const* me, const char* path) {
    return me->is_reg_file(me, path);
}

static inline bool fs_is_directory(Fs const* me, const char* path) {
    return me->is_directory(me, path);
}

// Returns true on success
static inline bool fs_for_each_item_in_dir(
    Fs const* me,
    const char* dir_path,
    void (*callback)(void* data, const char* dir_path, const char* name),
    void* data
) {
    return me->for_each_item_in_dir(me, dir_path, callback, data);
}

// Returns true on success. Gets an absolute path without symlinks, like realpath()
static inline bool fs_canonicalize_path(Fs const* me, const char* path, cstr* ret) {
    return me->canonicalize_path(me, path, ret);
}
// Returns true on success
static inline bool fs_read_entire_file(Fs const* me, const char* path, cstr* ret) {
    return me->read_entire_file(me, path, ret);
}

extern const Fs real_fs_noisy;
extern const Fs real_fs_quiet;
