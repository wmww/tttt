#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <linux/limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include "filesystem.h"
#include "string_helpers.h"

static const long read_file_len_limit = 1024l * 1024l * 1024l; // 1GB
const char path_sep = '/';

csview file_extension_of(csview path) {
    // -2 because -1 would be the last char, but if the last char is . we don't want to return ""
    for (const char* i = csview_end_ptr(path) - 2; i >= path.buf; i--) {
        if (*i == '/') {
            break;
        } else if (*i == '.') {
            return csview_from_range(i + 1, csview_end_ptr(path));
        }
    }
    return (csview){0};
}

csview basename_of(csview path) {
    // -2 because -1 would be the last char, but if the last char is / we don't want to return ""
    for (const char* i = csview_end_ptr(path) - 2; i >= path.buf; i--) {
        if (*i == '/') {
            return csview_from_range(i + 1, csview_end_ptr(path));
        }
    }
    if (path.size && *(csview_end_ptr(path) - 1) == '/') {
        path.size--;
    }
    return path;
}

csview dirname_of(csview path) {
    csview end = basename_of(path);
    if (end.buf == path.buf) {
        return c_sv(".");
    } else {
        return csview_from_range(path.buf, end.buf);
    }
}

// Returns the left-most path segment, or an empty csview if the path sarts with a separator. sets path start at the
// start of the next segment, or sets path to empty if there are no more segments
static csview path_next_segment(csview* path) {
    csview segment = *path;
    intptr_t segment_end = csview_find_sv(segment, csview_from_n(&path_sep, 1));
    if (segment_end != c_NPOS) {
        segment.size = segment_end;
    }
    const char* next_segment_start = csview_end_ptr(segment);
    while (next_segment_start < csview_end_ptr(*path) && *next_segment_start == path_sep) {
        next_segment_start++;
    }
    *path = csview_from_range(next_segment_start, csview_end_ptr(*path));
    return segment;
}

static void push_path_to_absolute(cstr* absolute, csview path) {
    while (path.size) {
        csview segment = path_next_segment(&path);
        if (!csview_size(segment) || csview_equals_sv(segment, c_sv("."))) {
        } else if (csview_equals_sv(segment, c_sv(".."))) {
            intptr_t i = cstr_size(absolute);
            while (i > 0) {
                i--;
                if (cstr_str(absolute)[i] == path_sep) {
                    break;
                }
            }
            if (i < 1) i = 1;
            cstr_resize(absolute, i, 0);
        } else {
            if (cstr_size(absolute) > 1) {
                cstr_append_n(absolute, &path_sep, 1);
            }
            cstr_append_sv(absolute, segment);
        }
    }
}

cstr absolute_path_of(csview cwd, csview path) {
    PANIC_IF(!cwd.size || *cwd.buf != path_sep, "cwd must be absolute path, is '%.*s'", c_SV(cwd));
    cstr ret = cstr_from_n(&path_sep, 1);
    if (*path.buf != path_sep) {
        push_path_to_absolute(&ret, cwd);
    }
    push_path_to_absolute(&ret, path);
    return ret;
}

cstr relative_path_of(csview cwd, csview path) {
    cstr absolute = absolute_path_of(cwd, path);
    path = cstr_sv(&absolute);
    while (cwd.size && path.size) {
        csview cwd_next = cwd;
        csview path_next = path;
        csview cwd_segment = path_next_segment(&cwd_next);
        csview path_segment = path_next_segment(&path_next);
        if (!csview_equals_sv(cwd_segment, path_segment)) {
            break;
        }
        cwd = cwd_next;
        path = path_next;
    }
    cstr ret = cstr_null;
    while (cwd.size) {
        cstr_append_lit(&ret, "..");
        cstr_append_n(&ret, &path_sep, 1);
        path_next_segment(&cwd);
    }
    if (path.size) {
        cstr_append_sv(&ret, path);
    }
    cstr_drop(&absolute);
    return ret;
}

static bool real_fs_is_reg_file(Fs const* me, csview path) {
    ignore me;
    PATH_FROM_CSVIEW(path_str, path, {
        return false;
    });
    struct stat buf_stat;
    if (stat(path_str, &buf_stat) < 0) {
        return false;
    }
    return S_ISREG(buf_stat.st_mode);
}

static bool real_fs_is_directory(Fs const* me, csview path) {
    ignore me;
    PATH_FROM_CSVIEW(path_str, path, {
        return false;
    });
    struct stat buf_stat;
    if (stat(path_str, &buf_stat) < 0) {
        return false;
    }
    return S_ISDIR(buf_stat.st_mode);
}

static bool real_fs_for_each_item_in_dir(
    Fs const* me,
    csview dir_path,
    void (*callback)(void* data, csview dir_path, csview name),
    void* data
) {
    ignore me;
    PATH_FROM_CSVIEW(dir_str, dir_path, {
        fprintf(stderr, "Invalid path '%.*s'\n", c_SV(dir_path));
        return false;
    });
    DIR* directory = opendir(dir_str);
    if (!directory) {
        fprintf(stderr, "Could not open '%.*s'\n", c_SV(dir_path));
        return false;
    }
    struct dirent* item;
    while ((item = readdir(directory))) {
        if (*item->d_name != '.' ||
            (strcmp(item->d_name, ".") != 0 && strcmp(item->d_name, ".."))
        ) {
            callback(data, dir_path, csview_from(item->d_name));
        }
    }
    if (closedir(directory) < 0) {
        fprintf(stderr, "Could not close '%.*s'\n", c_SV(dir_path));
        return false;
    }
    return true;
}

/*
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
*/

static cstr real_fs_get_cwd_internal(Fs const* me) {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd))) {
        return cstr_from(cwd);
    } else {
        if (my debug_on_fail) {
            fprintf(stderr, "Failed to determine current working directory\n");
        }
        return cstr_lit("/");
    }
}

static csview real_fs_get_cwd(Fs const* me) {
    // Threadsafety: make threadsafe
    static cstr cached_cwd = {0};
    if (!cstr_size(&cached_cwd)) {
        cached_cwd = real_fs_get_cwd_internal(me);
    }
    return cstr_sv(&cached_cwd);
}

static bool real_fs_read_entire_file(Fs const* me, csview path, cstr* ret) {
    if (!fs_is_reg_file(me, path)) {
        if (my debug_on_fail) {
            fprintf(stderr, "%.*s: not a file\n", c_SV(path));
        }
        return false;
    }
    PATH_FROM_CSVIEW(path_str, path, {
        fprintf(stderr, "%.*s: invalid path\n", c_SV(path));
        return false;
    });
    FILE* f = fopen(path_str, "rb");
    cstr target = cstr_null;
    if (!f) {
        if (my debug_on_fail) {
            fprintf(stderr, "%.*s: failed to open: %s\n", c_SV(path), strerror(errno));
        }
        goto err;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        if (my debug_on_fail) {
            fprintf(stderr, "%.*s: seek failed\n", c_SV(path));
        }
        goto err;
    };
    long len = ftell(f);
    if (len < 0) {
        if (my debug_on_fail) {
            fprintf(stderr, "%.*s: ftell failed: %s\n", c_SV(path), strerror(errno));
        }
        goto err;
    }
    if (len > read_file_len_limit - 1) {
        if (my debug_on_fail) {
            fprintf(
                stderr,
                "%.*s cannot be read because it is %.3fMB (max allowed is %.3fMB)\n",
                c_SV(path),
                len / (1024.0 * 1024.0),
                read_file_len_limit / (1024.0 * 1024.0));
        }
        goto err;
    } else if (len > 0) {
        if (fseek(f, 0, SEEK_SET) != 0) {
            if (my debug_on_fail) {
                fprintf(stderr, "%.*s: seek failed\n", c_SV(path));
            }
            goto err;
        };
        char* buf = cstr_resize(&target, len, 0);
        if (!buf) {
            if (my debug_on_fail) {
                fprintf(stderr, "%.*s: failed to allocate %.3fMB to read into\n", c_SV(path), len + 1.0);
            }
            goto err;
        }
        if (fread(buf, len, 1, f) != 1) {
            if (my debug_on_fail) {
                fprintf(stderr, "%.*s: failed to read\n", c_SV(path));
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
    .get_cwd = real_fs_get_cwd,
    .read_entire_file = real_fs_read_entire_file,
    .debug_on_fail = true,
};

Fs const real_fs_quiet = {
    .is_reg_file = real_fs_is_reg_file,
    .is_directory = real_fs_is_directory,
    .for_each_item_in_dir = real_fs_for_each_item_in_dir,
    .get_cwd = real_fs_get_cwd,
    .read_entire_file = real_fs_read_entire_file,
    .debug_on_fail = false,
};
