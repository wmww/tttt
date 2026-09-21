#pragma once

#include "prelude.h"

#include <cleat/filesystem.h>

typedef struct MockFs MockFs;

MockFs* mock_fs_new();
void mock_fs_destroy(MockFs* me);
// Used instead of fs_write_file() because this function takes ownership of the string (which prevents a copy). Unlike
// fs_write_file() it fails rather than overwriting a file that already exists. contents is dropped either way
failable mock_fs_create_file(MockFs* me, csview path, cstr contents);
// link becomes another name for whatever is (or will be) at target
failable mock_fs_create_symlink(MockFs* me, csview link, csview target);
// The path with every symlink on the way replaced by its target. ctx is the MockFs (the shape program_set_realpath() wants)
cstr mock_fs_realpath(void* ctx, csview path);
Fs const* mock_fs_get_fs(MockFs const* me);
