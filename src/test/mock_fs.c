#include "mock_fs.h"

static const char* mock_cwd = "/home/mockfs";

typedef struct MockFsNode MockFsNode;

wrap_forward_hmap_str(MockFsNode, mock_fs_node);

typedef struct MockFsNode {
    enum {
        MOCK_FS_NODE_FILE,
        MOCK_FS_NODE_DIR,
    } tag;
    union {
        cstr file;
        HmapStrMockFsNode dir;
    };
} MockFsNode;

void mock_fs_node_drop(MockFsNode* me);

#define i_val_class_names MockFsNode, mock_fs_node
#define i_key_str
#define i_is_forward
#define i_no_clone
#include "wrap_stc/hmap.h"

void mock_fs_node_drop(MockFsNode* me) {
    switch (my tag) {
        case MOCK_FS_NODE_FILE:
            cstr_drop(&my file);
            break;

        case MOCK_FS_NODE_DIR:
            hmap_str_mock_fs_node_drop(&my dir);
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

static inline MockFsNode* mock_fs_dir_add(MockFsNode* me, cstr name, MockFsNode node) {
    if (my tag != MOCK_FS_NODE_DIR) {
        NONFATAL("Mock fs: tried to add node to non-directory");
    }
    auto result = hmap_str_mock_fs_node_insert(&my dir, name, node);
    if (result.inserted) {
        return &result.ref->second;
    } else {
        NONFATAL("Mock fs: tried to add node where existing node already is");
        return nullptr;
    }
}

typedef struct MockFs {
    Fs super;
    MockFsNode root;
} MockFs;

static inline MockFsNode* lookup_node(MockFs const* me, const char* path, bool create_dirs) {
    cstr absolute = fs_absolute_path_of(&my super, path);
    MockFsNode* ret = c_const_cast(MockFsNode*, &my root);
    path = cstr_str(&absolute);
    while (path && ret) {
        if (ret->tag != MOCK_FS_NODE_DIR) {
            ret = nullptr;
            break;
        }
        const char* end = strchr(path, path_sep);
        cstr segment;
        if (end) {
            segment = cstr_from_n(path, end - path);
            path = end + 1;
        } else {
            segment = cstr_from(path);
            path = nullptr;
        }
        if (cstr_size(&segment)) {
            auto value = hmap_str_mock_fs_node_get_mut(&ret->dir, cstr_str(&segment));
            if (value) {
                ret = &value->second;
            } else if (create_dirs) {
                auto result = hmap_str_mock_fs_node_insert(&ret->dir, segment, mock_fs_dir());
                segment = cstr_null;
                ret = &result.ref->second;
            } else {
                ret = nullptr;
            }
        }
        cstr_drop(&segment);
    }
    cstr_drop(&absolute);
    return ret;
}

static bool mock_fs_is_reg_file(Fs const* super, const char* path) {
    MockFs const* me = c_container_of(super, MockFs, super);
    MockFsNode const* node = lookup_node(me, path, false);
    return node && node->tag == MOCK_FS_NODE_FILE;
}

static bool mock_fs_is_directory(Fs const* super, const char* path) {
    MockFs const* me = c_container_of(super, MockFs, super);
    MockFsNode const* node = lookup_node(me, path, false);
    return node && node->tag == MOCK_FS_NODE_DIR;
}

static bool mock_fs_for_each_item_in_dir(
    Fs const* super,
    const char* dir_path,
    void (*callback)(void* data, const char* dir_path, const char* name),
    void* data
) {
    MockFs const* me = c_container_of(super, MockFs, super);
    MockFsNode const* node = lookup_node(me, dir_path, false);
    if (!node || node->tag != MOCK_FS_NODE_DIR) {
        return false;
    }
    c_foreach(i, hmap_str_mock_fs_node, node->dir) {
        callback(data, dir_path, cstr_str(&i.ref->first));
    }
    return true;
}

const char* mock_fs_get_cwd(Fs const* super) {
    ignore super;
    return mock_cwd;
}

static bool mock_fs_read_entire_file(Fs const* super, const char* path, cstr* ret) {
    MockFs const* me = c_container_of(super, MockFs, super);
    MockFsNode const* node = lookup_node(me, path, false);
    if (!node || node->tag != MOCK_FS_NODE_FILE) {
        return false;
    }
    *ret = cstr_clone(node->file);
    return true;
}

MockFs* mock_fs_new() {
    return c_new(MockFs, {
        .super = {
            .is_reg_file = mock_fs_is_reg_file,
            .is_directory = mock_fs_is_directory,
            .for_each_item_in_dir = mock_fs_for_each_item_in_dir,
            .get_cwd = mock_fs_get_cwd,
            .read_entire_file = mock_fs_read_entire_file,
            .debug_on_fail = false,
        },
        .root = mock_fs_dir(),
    });
}

void mock_fs_destroy(MockFs* me) {
    mock_fs_node_drop(&my root);
    free(me);
}

void mock_fs_create_file(MockFs* me, const char* path, cstr contents) {
    cstr dir_path = cstr_from_sv(dirname_of(path));
    const char* filename = basename_of(path);
    MockFsNode* dir = lookup_node(me, cstr_str(&dir_path), true);
    if (dir) {
        mock_fs_dir_add(dir, cstr_from(filename), mock_fs_file(contents));
    } else {
        cstr_drop(&contents);
    }
    cstr_drop(&dir_path);
}

Fs const* mock_fs_get_fs(MockFs const* me) {
    return &my super;
}
