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

// The size of path's root, 0 if it's relative: `/`, and on Windows a drive (`C:/`, `C:`). (A UNC path's is `/`, as
// tttt's path_root_size() has it)
static intptr_t path_root_size_of(csview path) {
    if (!path.size) return 0;
#if defined _WIN32 || defined TTTT_WINDOWS_PATHS
    char c0 = path.buf[0] | 0x20;
    if (path.size >= 2 && c0 >= 'a' && c0 <= 'z' && path.buf[1] == ':') return path.size >= 3 && path.buf[2] == '/' ? 3 : 2;
#endif
    return *path.buf == path_sep ? 1 : 0;
}

#if defined _WIN32 || defined TTTT_WINDOWS_PATHS
// Windows' file names compare without case (ASCII here: what relative_path_of() needs of a drive or a directory)
static bool segment_equals(csview a, csview b) {
    if (a.size != b.size) return false;
    for (intptr_t i = 0; i < a.size; i++) {
        char x = a.buf[i] >= 'A' && a.buf[i] <= 'Z' ? a.buf[i] | 0x20 : a.buf[i];
        char y = b.buf[i] >= 'A' && b.buf[i] <= 'Z' ? b.buf[i] | 0x20 : b.buf[i];
        if (x != y) return false;
    }
    return true;
}
#else
#define segment_equals csview_equals_sv
#endif

// (absolute ends with its root, `root` bytes, which `..` doesn't go above)
static void push_path_to_absolute(cstr* absolute, intptr_t root, csview path) {
    while (path.size) {
        csview segment = path_next_segment(&path);
        if (!csview_size(segment) || csview_equals_sv(segment, c_sv("."))) {
        } else if (csview_equals_sv(segment, c_sv(".."))) {
            intptr_t i = cstr_size(absolute);
            while (i > root) {
                i--;
                if (cstr_str(absolute)[i] == path_sep) {
                    break;
                }
            }
            if (i < root) i = root;
            cstr_resize(absolute, i, 0);
        } else {
            if (cstr_size(absolute) > root) {
                cstr_append_n(absolute, &path_sep, 1);
            }
            cstr_append_sv(absolute, segment);
        }
    }
}

cstr absolute_path_of(csview cwd, csview path) {
    intptr_t path_root = path_root_size_of(path);
    csview rooted = path_root ? path : cwd;
    intptr_t root = path_root ? path_root : path_root_size_of(cwd);
    // (Not panic(): its logger makes paths absolute)
    if (!root) {
        fprintf(stderr, "cwd must be absolute path, is '%.*s'\n", (int)cwd.size, cwd.buf);
        abort();
    }
    cstr ret = cstr_from_sv(csview_slice(rooted, 0, root));
    // (A drive `C:` is its root `C:/`)
    if (cstr_str(&ret)[root - 1] != path_sep) {
        cstr_append_n(&ret, &path_sep, 1);
        root++;
    }
    if (!path_root) {
        push_path_to_absolute(&ret, root, csview_slice(cwd, path_root_size_of(cwd), cwd.size));
    }
    push_path_to_absolute(&ret, root, csview_slice(path, path_root, path.size));
    return ret;
}

cstr relative_path_of(csview cwd, csview path) {
    cstr absolute = absolute_path_of(cwd, path);
    path = cstr_sv(&absolute);
#if defined _WIN32 || defined TTTT_WINDOWS_PATHS
    // (On another drive or share: there's no relative path, the absolute one)
    intptr_t root = path_root_size_of(path), cwd_root = path_root_size_of(cwd);
    csview path_root = csview_slice(path, 0, root), dir_root = csview_slice(cwd, 0, cwd_root);
    if (path_root.size && path_root.buf[path_root.size - 1] == path_sep) path_root.size--;
    if (dir_root.size && dir_root.buf[dir_root.size - 1] == path_sep) dir_root.size--;
    if (!segment_equals(path_root, dir_root)) return absolute;
#endif
    while (cwd.size && path.size) {
        csview cwd_next = cwd;
        csview path_next = path;
        csview cwd_segment = path_next_segment(&cwd_next);
        csview path_segment = path_next_segment(&path_next);
        if (!segment_equals(cwd_segment, path_segment)) {
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
