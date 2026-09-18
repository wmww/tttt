#pragma once

#include "prelude.h"

#include <cleat/filesystem.h>

typedef struct MockFs MockFs;

MockFs* mock_fs_new();
void mock_fs_destroy(MockFs* me);
// Used instead of fs_write_file() because this function takes ownership of the string (which prevents a copy). Unlike
// fs_write_file() it fails rather than overwriting a file that already exists. contents is dropped either way
failable mock_fs_create_file(MockFs* me, csview path, cstr contents);
Fs const* mock_fs_get_fs(MockFs const* me);
