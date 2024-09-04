#pragma once

#include "filesystem.h"

typedef struct MockFilesystem {
    Filesystem super;
    cstr read_contents;
} MockFilesystem;

MockFilesystem* mock_filesystem_new();
void mock_filesystem_destroy(MockFilesystem* me);
