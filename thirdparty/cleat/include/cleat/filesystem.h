// See docs/filesystem.md for documentation

#pragma once

#include <stc/csview.h>
#include <stc/cstr.h>
#include <stdbool.h>
#include "failable_minimal.h"

typedef struct Fs Fs;

extern const char path_sep;

#define fs_path_from_csview(str, sv, on_fail) \
    if (!sv.size || sv.size > PATH_MAX - 1) { { on_fail } abort(); } \
    char str[PATH_MAX]; \
    memcpy(str, sv.buf, sv.size); \
    str[sv.size] = '\0';

csview file_extension_of(csview path);
csview basename_of(csview path);
csview dirname_of(csview path);
cstr absolute_path_of(csview cwd, csview path);
cstr relative_path_of(csview cwd, csview path);

typedef struct FsListdirIter FsListdirIter;
typedef struct FsListdirIter fs_listdir_iter;
typedef struct FsListdirIterImpl FsListdirIterImpl;
typedef struct FsListdirItem FsListdirItem;

// Mockable abstract filesystem class
struct Fs {
    bool (*exists)(Fs const* me, csview path);
    bool (*is_reg_file)(Fs const* me, csview path);
    bool (*is_directory)(Fs const* me, csview path);
    FsListdirIter (*start_listdir)(Fs const* me, csview dir_path);
    FailableResult (*get_cwd)(Fs const* me, returns cstr);
    FailableResult (*change_cwd)(Fs const* me, csview path);
    FailableResult (*read_file)(Fs const* me, csview path, returns cstr);
    FailableResult (*write_file)(Fs const* me, csview path, csview contents);
    FailableResult (*remove)(Fs const* me, csview path, bool recursive);
    FailableResult (*mkdir)(Fs const* me, csview path);
};

extern Fs const* const fs;

static inline bool fs_exists(Fs const* me, csview path) {
    return me->exists(me, path);
}

static inline bool fs_is_reg_file(Fs const* me, csview path) {
    return me->is_reg_file(me, path);
}

static inline bool fs_is_directory(Fs const* me, csview path) {
    return me->is_directory(me, path);
}

static_inline_failable fs_get_cwd(Fs const* me, returns cstr) {
    return me->get_cwd(me, _OUTPUT_PARAM);
}

static_inline_failable fs_change_cwd(Fs const* me, csview path) {
    return me->change_cwd(me, path);
}

// Returns an absolute path based on the filesystem's working directory
static_inline_failable fs_absolute_path_of(Fs const* me, csview path, returns cstr) {
    cstr working_dir;
    FailableResult result = fs_get_cwd(me, &working_dir);
    if (result.error) return result;
    cstr ret = absolute_path_of(cstr_sv(&working_dir), path);
    cstr_drop(&working_dir);
    *_OUTPUT_PARAM = ret;
    return (FailableResult){nullptr};
}

static_inline_failable fs_read_file(Fs const* me, csview path, returns cstr) {
    return me->read_file(me, path, _OUTPUT_PARAM);
}

static_inline_failable fs_write_file(Fs const* me, csview path, csview contents) {
    return me->write_file(me, path, contents);
}

static_inline_failable fs_mkdir(Fs const* me, csview path) {
    return me->mkdir(me, path);
}

static_inline_failable fs_remove(Fs const* me, csview path, bool recursive) {
    return me->remove(me, path, recursive);
}

// To iterate through all items in a directory use `for_each (item, fs_listdir, fs, c_sv("dir/path")) { ... }`. The
// following structs and helpers make that possible

struct FsListdirItem {
    // The directory we're enumerating the contents of
    csview dir;
    // The file name
    csview name;
    // The path including the directory and name
    csview path;
};

struct FsListdirIterImpl {
    void (*next)(FsListdirIter* me);
    void (*drop)(FsListdirIter* me);
    FsListdirItem current;
};

struct FsListdirIter {
    // Points to the implementation until it is destroyed, then null. Named ref for compatibility with STC container
    // iterators (in both cases (bool)iter.ref checks if iteration is complete)
    FsListdirIterImpl* ref;
};

static inline FsListdirIter fs_listdir_begin(Fs const* me, csview path) {
    return me->start_listdir(me, path);
}

static inline void fs_listdir_next(FsListdirIter* me) { me->ref->next(me); }

static inline FsListdirItem fs_listdir_iter_deref(FsListdirIter* iter) {
    return iter->ref ? iter->ref->current : (FsListdirItem){0};
}

// Must be called if breaking out of a loop early. Not needed if fs_listdir_next() is called until completion.
static inline void fs_listdir_iter_drop(FsListdirIter* me) { me->ref->drop(me); }

// `for_each (item, fs_listdir_sorted, fs, path)` buffers the item list, sorts it alphabetically and iterates through
// that.
FsListdirIter fs_listdir_sorted_begin(Fs const* me, csview path);
#define fs_listdir_sorted_iter fs_listdir_iter
#define fs_listdir_sorted_next fs_listdir_next
#define fs_listdir_sorted_iter_deref fs_listdir_iter_deref
