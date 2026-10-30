#pragma once
// Windows: <dirent.h> with d_type (MinGW-w64's has none), over FindFirstFileExW: names in UTF-8 (src/win32/win32.c). A
// symlink or junction is DT_UNKNOWN (stat it), as a symlink's d_type isn't its target's
#include <stddef.h>

#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12

struct dirent {
    unsigned char d_type;
    char d_name[1040]; // (MAX_PATH UTF-16 units, each up to 3 UTF-8 bytes, rounded up)
};
typedef struct DIR DIR;
DIR* opendir(char const* path);
struct dirent* readdir(DIR* dir);
int closedir(DIR* dir);
