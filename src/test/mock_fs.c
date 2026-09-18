#include "mock_fs.h"
#include "string_helpers.h"

typedef struct MockFsNode MockFsNode;

g_declare_hmap(SvStr, MockFsNode, mock_fs_node);

typedef struct MockFsNode {
    enum {
        MOCK_FS_NODE_FILE,
        MOCK_FS_NODE_DIR,
    } tag;
    union {
        cstr file;
        HMapMockFsNode dir;
    };
} MockFsNode;

void mock_fs_node_drop(MockFsNode* me);

#define g_pro_key SvStr, svstr
#define g_class_val MockFsNode, mock_fs_node
#define g_declared
#define g_no_clone
#include <cleat/generic/hmap.h>

void mock_fs_node_drop(MockFsNode* me) {
    switch (my tag) {
        case MOCK_FS_NODE_FILE:
            cstr_drop(&my file);
            break;

        case MOCK_FS_NODE_DIR:
            hmap_mock_fs_node_drop(&my dir);
            break;
    }
}

static inline MockFsNode mock_fs_file(cstr contents) {
    return (MockFsNode) {
        .tag = MOCK_FS_NODE_FILE,
        .file = contents
    };
}

static inline MockFsNode mock_fs_dir() {
    return (MockFsNode) {
        .tag = MOCK_FS_NODE_DIR,
        .dir = {0},
    };
}

typedef struct MockFs {
    Fs super;
    MockFsNode root;
    // Always an absolute path, starts out as the filesystem root
    cstr cwd;
} MockFs;

static inline MockFsNode* lookup_node(MockFs const* me, csview path, bool create_dirs) {
    // TODO: refactor to use csview more
    cstr absolute = absolute_path_of(cstr_sv(&my cwd), path.size ? path : c_sv("."));
    MockFsNode* ret = c_const_cast(MockFsNode*, &my root);
    const char* remaining = cstr_str(&absolute);
    while (remaining && ret) {
        if (ret->tag != MOCK_FS_NODE_DIR) {
            ret = nullptr;
            break;
        }
        const char* end = strchr(remaining, path_sep);
        csview segment;
        if (end) {
            segment = csview_from_range(remaining, end);
            remaining = end + 1;
        } else {
            segment = csview_from_range(remaining, csview_end_ptr(cstr_sv(&absolute)));
            remaining = nullptr;
        }
        if (segment.size) {
            auto value = hmap_mock_fs_node_get_mut(&ret->dir, segment);
            if (value) {
                ret = &value->second;
            } else if (create_dirs) {
                auto result = hmap_mock_fs_node_insert(&ret->dir, cstr_from_sv(segment), mock_fs_dir());
                ret = &result.ref->second;
            } else {
                ret = nullptr;
            }
        }
    }
    cstr_drop(&absolute);
    return ret;
}

// Takes ownership of contents. Overwrites the file if it already exists, creates parent directories as needed
static_failable mock_fs_set_file(MockFs* me, csview path, cstr contents) {
    csview name = basename_of(path);
    if (!name.size) {
        cstr_drop(&contents);
        return err_d(path, ": not a valid file path");
    }
    MockFsNode* dir = lookup_node(me, dirname_of(path), true);
    if (!dir || dir->tag != MOCK_FS_NODE_DIR) {
        cstr_drop(&contents);
        return err_d(path, ": parent directory could not be created");
    }
    auto existing = hmap_mock_fs_node_get_mut(&dir->dir, name);
    if (existing) {
        if (existing->second.tag != MOCK_FS_NODE_FILE) {
            cstr_drop(&contents);
            return err_d(path, ": is a directory");
        }
        cstr_take(&existing->second.file, contents);
    } else {
        hmap_mock_fs_node_insert(&dir->dir, cstr_from_sv(name), mock_fs_file(contents));
    }
    return ok;
}

static bool mock_fs_exists(Fs const* super, csview path) {
    MockFs const* me = c_container_of(super, MockFs, super);
    return lookup_node(me, path, false) != nullptr;
}

static bool mock_fs_is_reg_file(Fs const* super, csview path) {
    MockFs const* me = c_container_of(super, MockFs, super);
    MockFsNode const* node = lookup_node(me, path, false);
    return node && node->tag == MOCK_FS_NODE_FILE;
}

static bool mock_fs_is_directory(Fs const* super, csview path) {
    MockFs const* me = c_container_of(super, MockFs, super);
    MockFsNode const* node = lookup_node(me, path, false);
    return node && node->tag == MOCK_FS_NODE_DIR;
}

typedef struct MockFsListdirIterImpl {
    FsListdirIterImpl super;
    hmap_mock_fs_node_iter iter;
    cstr path;
} MockFsListdirIterImpl;

static void mock_fs_listdir_drop(FsListdirIter* me) {
    if (!my ref) return;
    MockFsListdirIterImpl* impl = (MockFsListdirIterImpl*)my ref;
    cstr_drop(&impl->path);
    free(impl);
    my ref = nullptr;
}

// Fills in the current item from the map iterator, or ends iteration if the map iterator is done
static void mock_fs_listdir_update(FsListdirIter* me) {
    MockFsListdirIterImpl* impl = (MockFsListdirIterImpl*)my ref;
    if (!impl->iter.ref) {
        mock_fs_listdir_drop(me);
        return;
    }
    my ref->current.name = cstr_sv(&impl->iter.ref->first);
    cstr_take(&impl->path, cstr_d(my ref->current.dir, path_sep, my ref->current.name));
    my ref->current.path = cstr_sv(&impl->path);
}

static void mock_fs_listdir_next(FsListdirIter* me) {
    if (!my ref) return;
    MockFsListdirIterImpl* impl = (MockFsListdirIterImpl*)my ref;
    hmap_mock_fs_node_next(&impl->iter);
    mock_fs_listdir_update(me);
}

static FsListdirIter mock_fs_start_listdir(Fs const* super, csview dir_path) {
    MockFs const* me = c_container_of(super, MockFs, super);
    MockFsNode const* node = lookup_node(me, dir_path, false);
    if (!node || node->tag != MOCK_FS_NODE_DIR) {
        return (FsListdirIter){0};
    }
    FsListdirIter ret = {
        .ref = &c_new(MockFsListdirIterImpl, {
            .super = {
                .next = mock_fs_listdir_next,
                .drop = mock_fs_listdir_drop,
                .current.dir = dir_path,
            },
            .iter = hmap_mock_fs_node_begin(&node->dir),
            .path = cstr_null,
        })->super,
    };
    mock_fs_listdir_update(&ret);
    return ret;
}

static_failable mock_fs_get_cwd(Fs const* super, returns cstr) {
    MockFs const* me = c_container_of(super, MockFs, super);
    return ok(cstr_clone(my cwd));
}

static_failable mock_fs_change_cwd(Fs const* super, csview path) {
    MockFs* me = c_container_of(super, MockFs, super);
    MockFsNode const* node = lookup_node(me, path, false);
    if (!node || node->tag != MOCK_FS_NODE_DIR) {
        return err_d(path, ": no such directory");
    }
    cstr_take(&my cwd, absolute_path_of(cstr_sv(&my cwd), path.size ? path : c_sv(".")));
    return ok;
}

static_failable mock_fs_read_file(Fs const* super, csview path, returns cstr) {
    MockFs const* me = c_container_of(super, MockFs, super);
    MockFsNode const* node = lookup_node(me, path, false);
    if (!node) {
        return err_d(path, ": no such file");
    }
    if (node->tag != MOCK_FS_NODE_FILE) {
        return err_d(path, ": not a file");
    }
    return ok(cstr_clone(node->file));
}

static_failable mock_fs_write_file(Fs const* super, csview path, csview contents) {
    MockFs* me = c_container_of(super, MockFs, super);
    return mock_fs_set_file(me, path, cstr_from_sv(contents));
}

static_failable mock_fs_remove(Fs const* super, csview path, bool recursive) {
    MockFs* me = c_container_of(super, MockFs, super);
    csview name = basename_of(path);
    MockFsNode* parent = name.size ? lookup_node(me, dirname_of(path), false) : nullptr;
    auto item = parent && parent->tag == MOCK_FS_NODE_DIR
        ? hmap_mock_fs_node_get_mut(&parent->dir, name)
        : nullptr;
    if (!item) {
        return err_d(path, ": no such file or directory");
    }
    if (item->second.tag == MOCK_FS_NODE_DIR &&
        !recursive &&
        !hmap_mock_fs_node_is_empty(&item->second.dir)
    ) {
        return err_d(path, ": directory not empty");
    }
    // Drops the node (and, for a directory, everything under it) along with its key
    hmap_mock_fs_node_erase(&parent->dir, name);
    return ok;
}

static_failable mock_fs_mkdir(Fs const* super, csview path) {
    MockFs* me = c_container_of(super, MockFs, super);
    MockFsNode const* node = lookup_node(me, path, true);
    if (!node) {
        return err_d(path, ": could not create directory");
    }
    if (node->tag != MOCK_FS_NODE_DIR) {
        return err_d(path, ": exists and is not a directory");
    }
    return ok;
}

MockFs* mock_fs_new() {
    return c_new(MockFs, {
        .super = {
            .exists = mock_fs_exists,
            .is_reg_file = mock_fs_is_reg_file,
            .is_directory = mock_fs_is_directory,
            .start_listdir = mock_fs_start_listdir,
            .get_cwd = mock_fs_get_cwd,
            .change_cwd = mock_fs_change_cwd,
            .read_file = mock_fs_read_file,
            .write_file = mock_fs_write_file,
            .remove = mock_fs_remove,
            .mkdir = mock_fs_mkdir,
        },
        .root = mock_fs_dir(),
        .cwd = cstr_from_n(&path_sep, 1),
    });
}

void mock_fs_destroy(MockFs* me) {
    mock_fs_node_drop(&my root);
    cstr_drop(&my cwd);
    free(me);
}

failable mock_fs_create_file(MockFs* me, csview path, cstr contents) {
    csview filename = basename_of(path);
    if (!filename.size) {
        cstr_drop(&contents);
        return err_d(path, ": not a valid file path");
    }
    MockFsNode* dir = lookup_node(me, dirname_of(path), true);
    if (!dir || dir->tag != MOCK_FS_NODE_DIR) {
        cstr_drop(&contents);
        return err_d(path, ": parent directory could not be created");
    }
    auto result = hmap_mock_fs_node_insert(&dir->dir, cstr_from_sv(filename), mock_fs_file(contents));
    if (!result.inserted) {
        return err_d(path, ": already exists");
    }
    return ok;
}

Fs const* mock_fs_get_fs(MockFs const* me) {
    return &my super;
}
