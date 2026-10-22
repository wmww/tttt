#pragma once

#include "prelude.h"
#include <cleat/filesystem.h>

// A filesystem whose existence probes (exists, is_reg_file, is_directory) are remembered by path: tsc's cached
// compiler host. Module resolution asks the same questions thousands of times (each node_modules up the tree, the
// directory of every relative import). Everything else is passed through; writing, removing, making a directory or
// changing the working directory forgets everything. Safe to probe from several threads at once
typedef struct CachedFs CachedFs;

// inner must outlive the cache
CachedFs* cached_fs_new(Fs const* inner);
void cached_fs_destroy(CachedFs* me);
Fs const* cached_fs_fs(CachedFs const* me);
Fs const* cached_fs_inner(CachedFs const* me);
// A listing of dir someone made anyway (the include walk's), kept as if a probe had listed it, unless one is known:
// each name's kind 1 a directory, 0 a regular file, -1 unknown (stat()ed when probed). Any thread
void cached_fs_offer_listing(CachedFs* me, csview dir, csview const* names, signed char const* is_directory, isize count);
