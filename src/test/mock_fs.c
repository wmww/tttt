#include "mock_fs.h"

typedef struct MockFs {
    Fs super;
    cstr read_contents;
} MockFs;

static bool mock_fs_is_reg_file(Fs const* me, const char* path) {
    ignore me;
    ignore path;
    return true;
}

static bool mock_fs_is_directory(Fs const* me, const char* path) {
    ignore me;
    ignore path;
    return false;
}

static bool mock_fs_for_each_item_in_dir(
    Fs const* me,
    const char* dir_path,
    void (*callback)(void* data, const char* dir_path, const char* name),
    void* data
) {
    ignore me;
    ignore dir_path;
    ignore callback;
    ignore data;
    return false;
}

static bool mock_fs_canonicalize_path(Fs const* super, const char* path, cstr* ret) {
    ignore super;
    *ret = cstr_from(path);
    return true;
}

static bool mock_fs_read_entire_file(Fs const* super, const char* path, cstr* ret) {
    ignore path;
    MockFs* me = c_container_of(super, MockFs, super);
    *ret = cstr_clone(my read_contents);
    return true;
}

MockFs* mock_fs_new() {
    return c_new(MockFs, {
        .super = {
            .is_reg_file = mock_fs_is_reg_file,
            .is_directory = mock_fs_is_directory,
            .for_each_item_in_dir = mock_fs_for_each_item_in_dir,
            .canonicalize_path = mock_fs_canonicalize_path,
            .read_entire_file = mock_fs_read_entire_file,
            .debug_on_fail = false,
        }
    });
}

void mock_fs_destroy(MockFs* me) {
    cstr_drop(&my read_contents);
    free(me);
}

void mock_fs_create_file(MockFs* me, const char* path, cstr contents) {
    ignore path;
    cstr_drop(&my read_contents);
    my read_contents = contents;
}

Fs const* mock_fs_get_fs(MockFs const* me) {
    return &my super;
}
