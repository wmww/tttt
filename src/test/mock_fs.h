#pragma once

#include "filesystem.h"

typedef struct MockFs {
    Fs super;
    cstr read_contents;
} MockFs;

MockFs* mock_fs_new();
void mock_fs_destroy(MockFs* me);
