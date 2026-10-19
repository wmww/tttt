// realpath() and PATH_MAX, which strict -std=c2x hides
#define _DEFAULT_SOURCE
#include "emit_store.h"
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>

// A path of an owner's: a file (the index of its text) or a directory one is in (-1)
typedef int32_t EmitEntry;
#define g_pro_key SvStr, svstr
#define g_val EmitEntry, emit_entry
#include <cleat/generic/hmap.h>

typedef struct EmitOwner {
    HMapEmitEntry paths;
    cstr* texts;
    u32 count, cap;
} EmitOwner;

struct EmitStore {
    EmitOwner* owners;
    u32 owner_count;
};

EmitStore* emit_store_new(u32 owner_count) {
    EmitStore* ret = c_new(EmitStore, { .owners = calloc(owner_count ? owner_count : 1, sizeof(EmitOwner)), .owner_count = owner_count });
    return ret;
}

void emit_store_destroy(EmitStore* me) {
    if (!me) return;
    for (u32 i = 0; i < my owner_count; i++) {
        hmap_emit_entry_drop(&my owners[i].paths);
        for (u32 t = 0; t < my owners[i].count; t++) cstr_drop(&my owners[i].texts[t]);
        free(my owners[i].texts);
    }
    free(my owners);
    free(me);
}

void emit_store_add(EmitStore* me, u32 owner, csview path, csview text) {
    check(owner < my owner_count);
    EmitOwner* o = &my owners[owner];
    auto existing = hmap_emit_entry_get(&o->paths, path);
    if (existing && existing->second >= 0) {
        cstr_assign_sv(&o->texts[existing->second], text);
        return;
    }
    if (o->count == o->cap) {
        o->cap = o->cap ? o->cap * 2 : 16;
        o->texts = realloc(o->texts, o->cap * sizeof(cstr));
    }
    o->texts[o->count] = cstr_from_sv(text);
    hmap_emit_entry_insert_or_assign(&o->paths, cstr_from_sv(path), (EmitEntry)o->count);
    o->count++;
    // (Every directory up to the root: they exist as far as the program's resolution is concerned)
    for (isize end = path.size - 1; end > 0; end--) {
        if (path.buf[end] != '/') continue;
        csview dir = csview_slice(path, 0, end);
        if (hmap_emit_entry_get(&o->paths, dir)) break;
        hmap_emit_entry_insert(&o->paths, cstr_from_sv(dir), -1);
    }
}

u32 emit_store_count(EmitStore const* me, u32 owner) {
    return owner < my owner_count ? my owners[owner].count : 0;
}

struct EmitView {
    Fs fs; // First: the Fs const* handed out is the view
    Fs const* inner;
    EmitStore const* store;
    u32* visible; // Owners seen, latest first (a path emitted twice is the later build's)
    u32 visible_count;
    cstr cwd; // What a relative path is relative to (the program asks relative to the working directory)
};

// The entry the view has at path (exactly), null if none; *text its owner's text when a file
static bool view_lookup_exact(EmitView const* me, csview path, EmitEntry* entry, cstr const** text) {
    for (u32 i = 0; i < my visible_count; i++) {
        EmitOwner const* o = &my store->owners[my visible[i]];
        auto found = hmap_emit_entry_get(&o->paths, path);
        if (!found) continue;
        *entry = found->second;
        if (text) *text = found->second >= 0 ? &o->texts[found->second] : nullptr;
        return true;
    }
    return false;
}

// realpath() of the deepest existing ancestor, the rest appended: where a path through a workspace link
// (node_modules/@scope/pkg -> packages/pkg) leads though its file is only in memory. Empty if nothing exists
static cstr lenient_real_path(csview path) {
    if (path.size >= PATH_MAX) return cstr_null;
    char buf[PATH_MAX];
    memcpy(buf, path.buf, (size_t)path.size);
    buf[path.size] = 0;
    char resolved[PATH_MAX];
    if (realpath(buf, resolved)) return cstr_from(resolved);
    isize slash = path.size;
    while (slash > 0 && path.buf[slash - 1] != '/') slash--;
    if (slash <= 1) return cstr_null;
    cstr parent = lenient_real_path(csview_slice(path, 0, slash - 1));
    if (!cstr_size(&parent)) return parent;
    cstr ret = cstr_d(parent, "/", csview_slice(path, slash, path.size));
    cstr_drop(&parent);
    return ret;
}

// The entry at path, also through a link (node_modules) whose target has it
static bool view_lookup(EmitView const* me, csview path, EmitEntry* entry, cstr const** text) {
    if (!my visible_count || !path.size) return false;
    cstr absolute = path.buf[0] == '/' ? cstr_null : absolute_path_of(cstr_sv(&my cwd), path);
    if (cstr_size(&absolute)) path = cstr_sv(&absolute);
    bool ret = view_lookup_exact(me, path, entry, text);
    if (!ret && csview_contains(path, "/node_modules/")) {
        cstr real = lenient_real_path(path);
        ret = cstr_size(&real) && !cstr_equals_sv(&real, path) && view_lookup_exact(me, cstr_sv(&real), entry, text);
        cstr_drop(&real);
    }
    cstr_drop(&absolute);
    return ret;
}

// A path with `..` in it, normalized for the inner file system: the kernel walks `dist/../..` only where dist exists on
// disk, which an in-memory one doesn't. Empty when path can go as it is
static cstr view_inner_path(EmitView const* me, csview path) {
    if (!my visible_count || !csview_contains(path, "..")) return cstr_null;
    return absolute_path_of(cstr_sv(&my cwd), path);
}
#define INNER_PATH(path, call) \
    cstr normalized_ = view_inner_path(me, path); \
    auto ret_ = call(my inner, cstr_size(&normalized_) ? cstr_sv(&normalized_) : path); \
    cstr_drop(&normalized_); \
    return ret_

static bool view_exists(Fs const* fs, csview path) {
    EmitView const* me = (EmitView const*)fs;
    EmitEntry entry;
    if (view_lookup(me, path, &entry, nullptr)) return true;
    INNER_PATH(path, fs_exists);
}

static bool view_is_reg_file(Fs const* fs, csview path) {
    EmitView const* me = (EmitView const*)fs;
    EmitEntry entry;
    if (view_lookup(me, path, &entry, nullptr) && entry >= 0) return true;
    INNER_PATH(path, fs_is_reg_file);
}

static bool view_is_directory(Fs const* fs, csview path) {
    EmitView const* me = (EmitView const*)fs;
    EmitEntry entry;
    if (view_lookup(me, path, &entry, nullptr) && entry < 0) return true;
    INNER_PATH(path, fs_is_directory);
}

static FsListdirIter view_start_listdir(Fs const* fs, csview dir_path) {
    return fs_listdir_begin(((EmitView const*)fs)->inner, dir_path);
}

static FailableResult view_get_cwd(Fs const* fs, returns cstr) {
    return fs_get_cwd(((EmitView const*)fs)->inner, _OUTPUT_PARAM);
}

static FailableResult view_change_cwd(Fs const* fs, csview path) {
    return fs_change_cwd(((EmitView const*)fs)->inner, path);
}

static FailableResult view_read_file(Fs const* fs, csview path, returns cstr) {
    EmitView const* me = (EmitView const*)fs;
    EmitEntry entry;
    cstr const* text = nullptr;
    if (view_lookup(me, path, &entry, &text) && text) {
        *_OUTPUT_PARAM = cstr_clone(*text);
        return (FailableResult){0};
    }
    cstr normalized = view_inner_path(me, path);
    FailableResult ret = fs_read_file(my inner, cstr_size(&normalized) ? cstr_sv(&normalized) : path, _OUTPUT_PARAM);
    cstr_drop(&normalized);
    return ret;
}

static FailableResult view_write_file(Fs const* fs, csview path, csview contents) {
    return fs_write_file(((EmitView const*)fs)->inner, path, contents);
}

static FailableResult view_remove(Fs const* fs, csview path, bool recursive) {
    return fs_remove(((EmitView const*)fs)->inner, path, recursive);
}

static FailableResult view_mkdir(Fs const* fs, csview path) {
    return fs_mkdir(((EmitView const*)fs)->inner, path);
}

EmitView* emit_view_new(EmitStore const* store, Fs const* inner, bool const* visible) {
    EmitView* ret = c_new(EmitView, {
        .fs = {
            .exists = view_exists,
            .is_reg_file = view_is_reg_file,
            .is_directory = view_is_directory,
            .start_listdir = view_start_listdir,
            .get_cwd = view_get_cwd,
            .change_cwd = view_change_cwd,
            .read_file = view_read_file,
            .write_file = view_write_file,
            .remove = view_remove,
            .mkdir = view_mkdir,
        },
        .inner = inner,
        .store = store,
    });
    try (fs_get_cwd(inner, &ret->cwd)) if_err (e) {
        ignore e;
        current_error_destroy();
        ret->cwd = cstr_from("/");
    }
    ret->visible = calloc(store->owner_count ? store->owner_count : 1, sizeof(u32));
    for (u32 i = store->owner_count; i-- > 0;) {
        if (visible[i]) ret->visible[ret->visible_count++] = i;
    }
    return ret;
}

void emit_view_destroy(EmitView* me) {
    if (!me) return;
    free(my visible);
    cstr_drop(&my cwd);
    free(me);
}

Fs const* emit_view_fs(EmitView const* me) {
    return &my fs;
}

cstr emit_view_real_path(void* ctx, csview path) {
    EmitView const* me = ctx;
    if (path.size >= PATH_MAX) return cstr_null;
    char buf[PATH_MAX];
    memcpy(buf, path.buf, (size_t)path.size);
    buf[path.size] = 0;
    char resolved[PATH_MAX];
    if (realpath(buf, resolved)) return cstr_from(resolved);
    // (A file only in memory: where its link leads)
    EmitEntry entry;
    if (!view_lookup(me, path, &entry, nullptr)) return cstr_null;
    cstr real = lenient_real_path(path);
    if (cstr_size(&real) && view_lookup_exact(me, cstr_sv(&real), &entry, nullptr)) return real;
    cstr_drop(&real);
    return cstr_from_sv(path);
}

i64 emit_view_file_size(void* ctx, csview path) {
    EmitView const* me = ctx;
    EmitEntry entry;
    cstr const* text = nullptr;
    if (view_lookup(me, path, &entry, &text) && text) return (i64)cstr_size(text);
    if (path.size >= PATH_MAX) return 0;
    char buf[PATH_MAX];
    memcpy(buf, path.buf, (size_t)path.size);
    buf[path.size] = 0;
    struct stat info;
    return stat(buf, &info) ? 0 : (i64)info.st_size;
}
