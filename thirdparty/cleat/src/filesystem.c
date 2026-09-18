#include "string_helpers.h"
#include "filesystem.h"
#include "common_macros.h"
#include "log.h"

const char path_sep = '/';

csview file_extension_of(csview path) {
    // -2 because -1 would be the last char, but if the last char is . we don't want to return ""
    for (const char* i = csview_end_ptr(path) - 2; i >= path.buf + 1; i--) {
        if (*i == path_sep || *(i - 1) == path_sep) {
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
        if (*i == path_sep) {
            path = csview_from_range(i + 1, csview_end_ptr(path));
            break;
        }
    }
    if (path.size && *(csview_end_ptr(path) - 1) == path_sep) {
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
    panic_if(!cwd.size || *cwd.buf != path_sep, "cwd must be absolute path, is '", cwd, "'");
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

typedef struct SortedListdirIterImpl {
    FsListdirIterImpl super;
    cstr dir;
    VecCStr paths;
    i64 i;
} SortedListdirIterImpl;

static void sorted_listdir_drop(FsListdirIter* me) {
    if (!my ref) return;
    SortedListdirIterImpl* impl = (SortedListdirIterImpl*)my ref;
    cstr_drop(&impl->dir);
    vec_cstr_drop(&impl->paths);
    free(impl);
    my ref = nullptr;
}

static void sorted_listdir_next(FsListdirIter* me) {
    if (!my ref) return;
    SortedListdirIterImpl* impl = (SortedListdirIterImpl*)my ref;
    impl->i++;
    if (impl->i < vec_cstr_size(&impl->paths)) {
        csview path = cstr_sv(vec_cstr_at(&impl->paths, impl->i));
        my ref->current = (FsListdirItem){
            .dir = cstr_sv(&impl->dir),
            .name = basename_of(path),
            .path = path,
        };
    } else {
        sorted_listdir_drop(me);
    }
}

FsListdirIter fs_listdir_sorted_begin(Fs const* me, csview path) {
    VecCStr paths = vec_cstr_init();
    for_each (item, fs_listdir, me, path) {
        vec_cstr_push(&paths, cstr_from_sv(item.path));
    }
    vec_cstr_sort(&paths);
    FsListdirIter ret = {
        .ref = &c_new(SortedListdirIterImpl, {
            .super = {
                .next = sorted_listdir_next,
                .drop = sorted_listdir_drop,
            },
            .dir = cstr_from_sv(path),
            .paths = paths,
            .i = -1,
        })->super,
    };
    sorted_listdir_next(&ret);
    return ret;
}
