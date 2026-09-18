# Filesystem
[filesystem.h](../include/cleat/filesystem.h) contains the filesystem API. It's currently only implemented for Linux but designed to be portable. It's implemented as virtual methods on an abstract `Fs` struct, so various custom usecases can be supported (validity checks before write, in-memory mock filesystem for tests, etc). The `fs` global is the platform's default implementation. The filesystem API uses normal cleat [error handling](error_handling.md) and STC string types. Here's the basic usage:

```c
#include <cleat/filesystem.h>
#include <cleat/failable.h>

failable create_and_read_file(csview contents, returns cstr) {
    csview const path = c_sv("file.txt");
    if (fs_exists(fs, path)) {
        try (cstr data, fs_read_file(fs, path, &data)) bubble;
        return ok(data);
    } else {
        try (fs_write_file(fs, path, contents)) bubble;
        return ok(cstr_from_sv(contents));
    }
}
```

## API reference:
These functions do not need an instance of `Fs` and do not make calls into the operating system.
```c
#include <cleat/filesystem.h>

// Get the file extension of the given file/path (excluding .), or none if it doesn't have a file extension
csview file_extension_of(csview path);

// Returns the final component of the given path
csview basename_of(csview path);

// Returns everything but the final component of the given path, includes the final /
csview dirname_of(csview path);

// Converts relative path to an absolute path given a current working directory (cwd). Eliminates . and .., does not
// resolve symlinks or ~. cwd must be an absolute path. path may be absolute already, in which case it is returned
// unmodified. fs_absolute_path_of() can be used to use the real cwd from the OS.
cstr absolute_path_of(csview cwd, csview path);

// Converts relative or absolute path into a simplified relative path. cwd (current working directory) must be an
// absolute path.
cstr relative_path_of(csview cwd, csview path);
```

These functions take a `Fs`, and do call into the operating system for the default fs.
```c
#include <cleat/filesystem.h>

// Returns true if the given path exists
static inline bool fs_exists(Fs const* me, csview path);

// Returns true if the given path is a regular file
static inline bool fs_is_reg_file(Fs const* me, csview path);

// Returns true if the given path is a directory
static inline bool fs_is_directory(Fs const* me, csview path);

// Returns the current working directory
static_inline_failable fs_get_cwd(Fs const* me, returns cstr);

// Changes the current working directory
static_inline_failable fs_change_cwd(Fs const* me, csview path);

// Converts a path into an absolute path based on the current working directory
static_inline_failable fs_absolute_path_of(Fs const* me, csview path, returns cstr);

// Reads the entire contents of a file into a string
static_inline_failable fs_read_file(Fs const* me, csview path, returns cstr);

// Writes the entire contents of a file from a string
static_inline_failable fs_write_file(Fs const* me, csview path, csview contents);

// Creates a directory, along with parent directories if needed
static_inline_failable fs_mkdir(Fs const* me, csview path);

// Deletes a file or directory, only deletes non-empty directories if recursive is true
static_inline_failable fs_remove(Fs const* me, csview path, bool recursive);
```

Enumerating directory contents is done with iterators, generally using the `for_each` statement from `common_macros.h`. `fs_listdir_iter_drop()` must be used if breaking out of the loop/returning early.
```c
#include <cleat/filesystem.h>
#include <cleat/common_macros.h>

cstr find_text_file(csview dir) {
    for_each (item, fs_listdir, fs, dir) {
        if (csview_equals(file_extension_of(item.name), "txt")) {
            fs_listdir_iter_drop(&item_iter);
            return cstr_from_sv(item.path);
        }
    }
    // If order matters, there is a version that sorts items by name
    for_each (item, fs_listdir_sorted, fs, dir) {
        if (csview_equals(file_extension_of(item.name), "txt")) {
            fs_listdir_iter_drop(&item_iter);
            return cstr_from_sv(item.path);
        }
    }
    return cstr_null;
}
```
