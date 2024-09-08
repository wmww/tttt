#pragma once

#include "prelude.h"

// Returns a pointer into the given string, returns nullptr if it has no extension
const char* file_extension_of(const char* path);
// Returns the final component of the given path, or "" if the path ends in a /
const char* basename_of(const char* path);
// Returns everything but the final component of the given path, includes the final /
csview dirname_of(const char* path);
// Converts relative paths to absolute paths and eliminates . and .., does not resolve symlinks or ~. cwd (current
// working directory) must be an absolute path.
cstr absolute_path_of(const char* cwd, const char* path);

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
    const char* (*get_cwd)(Fs const* me);
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

// Returns the current working directory
static inline const char* fs_get_cwd(Fs const* me) {
    return me->get_cwd(me);
}

// Returns an absolute path based on the filesystem's working directory
static inline cstr fs_absolute_path_of(Fs const* me, const char* path) {
    return absolute_path_of(fs_get_cwd(me), path);
}

// Returns true on success
static inline bool fs_read_entire_file(Fs const* me, const char* path, cstr* ret) {
    return me->read_entire_file(me, path, ret);
}

extern const Fs real_fs_noisy;
extern const Fs real_fs_quiet;
