#define _XOPEN_SOURCE 700
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <linux/limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#include "filesystem.h"

static const long read_file_len_limit = 1024l * 1024l * 1024l; // 1GB

const char* file_extension_of(const char* path) {
    const char* ret = "";
    for (const char* i = path; *i; i++) {
        if (*i == '/') {
            ret = "";
        } else if (*i == '.' && *(i + 1)) {
            ret = i + 1;
        }
    }
    return ret;
}

const char* basename_of(const char* path) {
    const char* ret = path;
    for (const char* i = path; *i; i++) {
        if (*i == '/') {
            ret = i + 1;
        }
    }
    return ret;
}

csview dirname_of(const char* path) {
    return c_sv(path, basename_of(path) - path);
}

bool real_fs_is_reg_file(Fs const* me, const char* path) {
    ignore me;
    struct stat buf_stat;
    if (!path || stat(path, &buf_stat) < 0) {
        return false;
    }
    return S_ISREG(buf_stat.st_mode);
}

bool real_fs_is_directory(Fs const* me, const char* path) {
    ignore me;
    struct stat buf_stat;
    if (!path || stat(path, &buf_stat) < 0) {
        return false;
    }
    return S_ISDIR(buf_stat.st_mode);
}

bool real_fs_for_each_item_in_dir(
    Fs const* me,
    const char* dir_path,
    void (*callback)(void* data, const char* dir_path, const char* name),
    void* data
) {
    ignore me;
    DIR* directory = opendir(dir_path);
    if (!directory) {
        fprintf(stderr, "Could not open %s\n", dir_path);
        return false;
    }
    struct dirent* item;
    while ((item = readdir(directory))) {
        if (*item->d_name != '.' ||
            (strcmp(item->d_name, ".") != 0 && strcmp(item->d_name, ".."))
        ) {
            callback(data, dir_path, item->d_name);
        }
    }
    if (closedir(directory) < 0) {
        fprintf(stderr, "Could not close %s\n", dir_path);
        return false;
    }
    return true;
}

static bool real_fs_canonicalize_path(Fs const* me, const char* path, cstr* ret) {
    char buffer[PATH_MAX];
    if (realpath(path, buffer)) {
        *ret = cstr_from(buffer);
        return true;
    } else {
        if (my debug_on_fail) {
            fprintf(
                stderr,
                "Could not canonicalize %s: %s\n",
                path,
                strerror(errno)
            );
        }
        return false;
    }
}

static bool real_fs_read_entire_file(Fs const* me, const char* path, cstr* ret) {
    if (!fs_is_reg_file(me, path)) {
        if (my debug_on_fail) {
            fprintf(stderr, "%s: not a file\n", path);
        }
        return false;
    }
    FILE* f = fopen(path, "rb");
    cstr target = cstr_null;
    if (!f) {
        if (my debug_on_fail) {
            fprintf(stderr, "%s: failed to open: %s\n", path, strerror(errno));
        }
        goto err;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        if (my debug_on_fail) {
            fprintf(stderr, "%s: seek failed\n", path);
        }
        goto err;
    };
    long len = ftell(f);
    if (len < 0) {
        if (my debug_on_fail) {
            fprintf(stderr, "%s: ftell failed: %s\n", path, strerror(errno));
        }
        goto err;
    }
    if (len > read_file_len_limit - 1) {
        if (my debug_on_fail) {
            fprintf(
                stderr,
                "%s cannot be read because it is %.3fMB (max allowed is %.3fMB)\n",
                path,
                len / (1024.0 * 1024.0),
                read_file_len_limit / (1024.0 * 1024.0));
        }
        goto err;
    } else if (len > 0) {
        if (fseek(f, 0, SEEK_SET) != 0) {
            if (my debug_on_fail) {
                fprintf(stderr, "%s: seek failed\n", path);
            }
            goto err;
        };
        char* buf = cstr_resize(&target, len, 0);
        if (!buf) {
            if (my debug_on_fail) {
                fprintf(stderr, "%s: failed to allocate %.3fMB to read into\n", path, len + 1.0);
            }
            goto err;
        }
        if (fread(buf, len, 1, f) != 1) {
            if (my debug_on_fail) {
                fprintf(stderr, "%s: failed to read\n", path);
            }
            goto err;
        }
    }
    fclose(f);
    f = nullptr;
    *ret = cstr_null;
    cstr_take(ret, target);
    return true;

err:
    if (f) {
        fclose(f);
    }
    cstr_drop(&target);
    return false;
}

Fs const real_fs_noisy = {
    .is_reg_file = real_fs_is_reg_file,
    .is_directory = real_fs_is_directory,
    .for_each_item_in_dir = real_fs_for_each_item_in_dir,
    .canonicalize_path = real_fs_canonicalize_path,
    .read_entire_file = real_fs_read_entire_file,
    .debug_on_fail = true,
};

Fs const real_fs_quiet = {
    .is_reg_file = real_fs_is_reg_file,
    .is_directory = real_fs_is_directory,
    .for_each_item_in_dir = real_fs_for_each_item_in_dir,
    .canonicalize_path = real_fs_canonicalize_path,
    .read_entire_file = real_fs_read_entire_file,
    .debug_on_fail = false,
};
