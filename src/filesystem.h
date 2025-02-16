#pragma once

#include "prelude.h"

extern const char path_sep;

#define PATH_FROM_CSVIEW(str, sv, on_fail) \
    if (!sv.size || sv.size > PATH_MAX - 1) { on_fail } \
    char str[PATH_MAX]; \
    memcpy(str, sv.buf, sv.size); \
    str[sv.size] = '\0';

// Returns a pointer into the given string, returns nullptr if it has no extension
csview file_extension_of(csview path);
// Returns the final component of the given path, or "" if the path ends in a /
csview basename_of(csview path);
// Returns everything but the final component of the given path, includes the final /
csview dirname_of(csview path);
// Converts relative paths to absolute paths and eliminates . and .., does not resolve symlinks or ~. cwd (current
// working directory) must be an absolute path.
cstr absolute_path_of(csview cwd, csview path);
// Converts relative or absolute path into a simplified relative path from cwd. cwd (current working directory) must be
// an absolute path.
cstr relative_path_of(csview cwd, csview path);

// Allows for virtual filesystems for tests
typedef struct Fs Fs;
typedef struct Fs {
    bool (*is_reg_file)(Fs const* me, csview path);
    bool (*is_directory)(Fs const* me, csview path);
    bool (*for_each_item_in_dir)(
        Fs const* me,
        csview dir_path,
        void (*callback)(void* data, csview dir_path, csview name),
        void* data);
    csview (*get_cwd)(Fs const* me);
    bool (*read_entire_file)(Fs const* me, csview path, cstr* ret);
    bool (*write_entire_file)(Fs const* me, csview path, csview contents);
    bool debug_on_fail;
} Fs;

static inline bool fs_is_reg_file(Fs const* me, csview path) {
    return me->is_reg_file(me, path);
}

static inline bool fs_is_directory(Fs const* me, csview path) {
    return me->is_directory(me, path);
}

// Returns true on success
static inline bool fs_for_each_item_in_dir(
    Fs const* me,
    csview dir_path,
    void (*callback)(void* data, csview dir_path, csview name),
    void* data
) {
    return me->for_each_item_in_dir(me, dir_path, callback, data);
}

// Returns the current working directory
static inline csview fs_get_cwd(Fs const* me) {
    return me->get_cwd(me);
}

// Returns an absolute path based on the filesystem's working directory
static inline cstr fs_absolute_path_of(Fs const* me, csview path) {
    return absolute_path_of(fs_get_cwd(me), path);
}

// Returns true on success
static inline bool fs_read_entire_file(Fs const* me, csview path, cstr* ret) {
    return me->read_entire_file(me, path, ret);
}

// Returns true on success
static inline bool fs_write_entire_file(Fs const* me, csview path, csview contents) {
    return me->write_entire_file(me, path, contents);
}

extern const Fs real_fs_noisy;
extern const Fs real_fs_quiet;
