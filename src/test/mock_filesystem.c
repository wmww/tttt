#include "mock_filesystem.h"

static bool mock_filesystem_canonicalize_path(Filesystem const* super, const char* path, cstr* ret) {
    ignore super;
    *ret = cstr_from(path);
    return true;
}

static bool mock_filesystem_read_entire_file(Filesystem const* super, const char* path, cstr* ret) {
    ignore path;
    MockFilesystem* me = c_container_of(super, MockFilesystem, super);
    *ret = cstr_clone(my read_contents);
    return true;
}

MockFilesystem* mock_filesystem_new() {
    return c_new(MockFilesystem, {
        .super = {
            .canonicalize_path = mock_filesystem_canonicalize_path,
            .read_entire_file = mock_filesystem_read_entire_file,
            .debug_on_fail = false,
        }
    });
}

void mock_filesystem_destroy(MockFilesystem* me) {
    cstr_drop(&my read_contents);
    free(me);
}
