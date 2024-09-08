#pragma once

#include "filesystem.h"

typedef struct MockFs MockFs;

MockFs* mock_fs_new();
void mock_fs_destroy(MockFs* me);
void mock_fs_create_file(MockFs* me, const char*, cstr contents);
Fs const* mock_fs_get_fs(MockFs const* me);
