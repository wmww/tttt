#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#ifdef __linux__
#include <linux/limits.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include "filesystem.h"
#include "common_macros.h"
#include "failable.h"

#define filesystem_error_new string_error_new

static bool real_fs_exists(Fs const* me, csview path) {
    ignore me;
    fs_path_from_csview(path_str, path, {
        return false;
    });
    struct stat buf_stat;
    return stat(path_str, &buf_stat) >= 0;
}

static bool real_fs_is_reg_file(Fs const* me, csview path) {
    ignore me;
    fs_path_from_csview(path_str, path, {
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
    fs_path_from_csview(path_str, path, {
        return false;
    });
    struct stat buf_stat;
    if (stat(path_str, &buf_stat) < 0) {
        return false;
    }
    return S_ISDIR(buf_stat.st_mode);
}

typedef struct RealFsListdirIterImpl {
    FsListdirIterImpl super;
    Fs const* fs;
    DIR* directory;
    cstr path;
} RealFsListdirIterImpl;

static void real_fs_listdir_drop(FsListdirIter* me) {
    if (!my ref) return;
    RealFsListdirIterImpl* impl = (RealFsListdirIterImpl*)my ref;
    if (closedir(impl->directory) < 0) {
        // ignore
    }
    cstr_drop(&impl->path);
    free(impl);
    my ref = nullptr;
}

static void real_fs_listdir_next(FsListdirIter* me) {
    if (!my ref) return;
    RealFsListdirIterImpl* impl = (RealFsListdirIterImpl*)my ref;
    struct dirent* item = readdir(impl->directory);
    if (item) {
        if (*item->d_name == '.' &&
            (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0)
        ) {
            real_fs_listdir_next(me);
            return;
        }
        my ref->current.name = csview_from(item->d_name);
        cstr_take(&impl->path, cstr_from_fmt(
            "%.*s%c%.*s",
            c_SV(my ref->current.dir),
            path_sep,
            c_SV(my ref->current.name)
        ));
        my ref->current.path = cstr_sv(&impl->path);
    } else {
        real_fs_listdir_drop(me);
    }
}

static FsListdirIter real_fs_start_listdir(Fs const* me, csview dir_path) {
    fs_path_from_csview(dir_str, dir_path, {
        return (FsListdirIter){0};
    });
    DIR* directory = opendir(dir_str);
    if (!directory) {
        return (FsListdirIter){0};
    }
    FsListdirIter ret = {
        .ref = &c_new(RealFsListdirIterImpl, {
            .super = {
                .next = real_fs_listdir_next,
                .drop = real_fs_listdir_drop,
                .current.dir = dir_path,
            },
            .fs = me,
            .directory = directory,
        })->super,
    };
    real_fs_listdir_next(&ret);
    return ret;
}

static_failable real_fs_get_cwd(Fs const* me, returns cstr) {
    ignore me;
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd))) {
        return ok(cstr_from(cwd));
    } else {
        return err(filesystem_error_new(cstr_lit("Failed to determine current working directory")));
    }
}

static_failable real_fs_change_cwd(Fs const* me, csview path) {
    ignore me;
    fs_path_from_csview(path_str, path, {
        return err(filesystem_error_new(cstr_from_fmt("%.*s: invalid path", c_SV(path))));
    });
    if (chdir(path_str) != 0) {
        return err(filesystem_error_new(cstr_from_fmt("%.*s: chdir failed", c_SV(path))));
    }
    return ok;
}

static_failable read_file_inner(csview path, FILE* f, cstr* target) {
    // Use the seek/ftell size as an allocation hint: it is exact for real
    // regular files but unreliable for virtual procfs/sysfs files, which report
    // a size of 0 or a page size that does not match their readable content
    long hint = 0;
    if (fseek(f, 0, SEEK_END) == 0) {
        long end = ftell(f);
        if (end > 0) {
            hint = end;
        }
        if (fseek(f, 0, SEEK_SET) != 0) {
            return err(filesystem_error_new(cstr_from_fmt("%.*s: seek failed", c_SV(path))));
        }
    }

    isize_t cap = hint + 1;
    isize_t total = 0;
    char* buf = cstr_resize(target, cap, 0);
    if (!buf) {
        return err(filesystem_error_new(cstr_from_fmt(
            "%.*s: failed to allocate %.3fMB to read into",
            c_SV(path),
            (cap + 1.0) / 1000000)
        ));
    }
    while (true) {
        total += (isize_t)fread(buf + total, 1, (size_t)(cap - total), f);
        if (total < cap) {
            // Short read: either EOF (done) or a stream error.
            if (ferror(f)) {
                return err(filesystem_error_new(cstr_from_fmt("%.*s: failed to read", c_SV(path))));
            }
            break;
        }
        // Buffer full; there may be more data (hint was too small or absent)
        cap *= 2;
        buf = cstr_resize(target, cap, 0);
        if (!buf) {
            return err(filesystem_error_new(cstr_from_fmt(
                "%.*s: failed to allocate %.3fMB to read into",
                c_SV(path),
                (cap + 1.0) / 1000000)
            ));
        }
    }
    cstr_resize(target, total, 0);
    return ok;
}

static_failable real_fs_read_file(Fs const* me, csview path, returns cstr) {
    if (!real_fs_is_reg_file(me, path)) {
        return err(filesystem_error_new(cstr_from_fmt("%.*s: not a file", c_SV(path))));
    }
    fs_path_from_csview(path_str, path, {
        return err(filesystem_error_new(cstr_from_fmt("%.*s: invalid path", c_SV(path))));
    });
    FILE* f = fopen(path_str, "rb");
    if (!f) {
        return err(filesystem_error_new(cstr_from_fmt(
            "%.*s: failed to open: %s",
            c_SV(path),
            strerror(errno)
        )));
    }
    cstr contents = cstr_null;
    try (read_file_inner(path, f, &contents)) cleanup {
        fclose(f);
        cstr_drop(&contents);
    }
    fclose(f);
    return ok(contents);
}

static_failable real_fs_write_file(Fs const* me, csview path, csview contents) {
    ignore me;
    fs_path_from_csview(path_str, path, {
        return err(filesystem_error_new(cstr_from_fmt("%.*s: invalid path", c_SV(path))));
    });
    FILE *f = fopen(path_str, "wb");
    if (!f) {
        return err(filesystem_error_new(cstr_from_fmt("%.*s: failed to open: %s", c_SV(path), strerror(errno))));
    }
    if (fwrite(contents.buf, 1, contents.size, f) != (size_t)contents.size) {
        fclose(f);
        return err(filesystem_error_new(cstr_from_fmt("%.*s: writing file: %s", c_SV(path), strerror(errno))));
    }
    fclose(f);
    return ok;
}

static_failable real_fs_mkdir(Fs const* me, csview path) {
    csview parent = dirname_of(path);
    if (!real_fs_is_directory(me, parent)) {
        if (csview_equals(parent, ".") || csview_equals(parent, "/")) {
            return err(filesystem_error_new(cstr_from_fmt(
                "cannot create directory, parent %.*s does not exist",
                c_SV(parent)
            )));
        } else {
            try (real_fs_mkdir(me, parent)) bubble;
        }
    }
    fs_path_from_csview(path_str, path, {
        return err(filesystem_error_new(cstr_from_fmt("%.*s: invalid path", c_SV(path))));
    });
    if (mkdir(path_str, 0755) != 0) {
        if (errno == EEXIST) {
            return ok;
        } else {
            return err(filesystem_error_new(cstr_from_fmt(
                "%.*s: creating directory: %s",
                c_SV(path),
                strerror(errno)
            )));
        }
    }
    return ok;
}

static_failable real_fs_remove(Fs const* me, csview path, bool recursive) {
    if (real_fs_is_directory(me, path) && recursive) {
        for_each (i, fs_listdir, me, path) {
            try (real_fs_remove(me, i.path, recursive)) cleanup {
                fs_listdir_iter_drop(&i_iter);
            }
        }
    }
    fs_path_from_csview(path_str, path, {
        return err(filesystem_error_new(cstr_from_fmt("%.*s: invalid path", c_SV(path))));
    });
    if (remove(path_str) != 0) {
        return err(filesystem_error_new(cstr_from_fmt(
            "%.*s: removing: %s",
            c_SV(path),
            strerror(errno)
        )));
    }
    return ok;
}

Fs const real_fs_impl = {
    .exists = real_fs_exists,
    .is_reg_file = real_fs_is_reg_file,
    .is_directory = real_fs_is_directory,
    .start_listdir = real_fs_start_listdir,
    .get_cwd = real_fs_get_cwd,
    .change_cwd = real_fs_change_cwd,
    .read_file = real_fs_read_file,
    .write_file = real_fs_write_file,
    .mkdir = real_fs_mkdir,
    .remove = real_fs_remove,
};

Fs const* const fs = &real_fs_impl;
