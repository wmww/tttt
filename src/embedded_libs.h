#pragma once

#include "prelude.h"

// The standard library (tsgo's bundled libs, thirdparty/lib) compiled into the binary (embedded_libs.py generates the
// table at build time): no file is read for it, so a copied binary works anywhere. The program loads them by absolute
// path in a directory no real file is read from, EMBEDDED_LIB_DIR (`/.tttt/libs/es5.d.ts`): program_make_source() takes
// a path there from this table, never from the filesystem. How messages name them is tsgo's
// (`bundled:///libs/lib.es5.d.ts`: SourceFile.bundled_path), never this path
#define EMBEDDED_LIB_DIR "/.tttt/libs"

typedef struct EmbeddedLib {
    char const* name; // `es5.d.ts`, `dom.d.ts`, `default.d.ts` (lib.d.ts)
    char const* text; // Zero terminated
    u32 size;
} EmbeddedLib;

// Sorted by name
extern EmbeddedLib const embedded_libs[];
extern u32 const embedded_lib_count;

// The lib file at an absolute normalized path in EMBEDDED_LIB_DIR, null for any other path
EmbeddedLib const* embedded_lib_at(csview path);
