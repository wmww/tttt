#pragma once

#include "ast_alloc.h"
#include "expr.h"

typedef struct ErrorCtx ErrorCtx;

// `with { type: "json" }` (or the older `assert { ... }`) after a module specifier, and `{ with: { ... } }` in an
// import type
typedef struct ImportAttribute {
    Span name; // As written: an identifier or a string literal with its quotes
    Expr value;
} ImportAttribute;

void import_attribute_drop(ImportAttribute* me);

#define g_class_key ImportAttribute, import_attribute
#define g_no_clone
#define g_allocator ast
#include <cleat/generic/vec.h>

typedef struct ImportAttributes {
    Span span; // From `with`/`assert` to the `}`
    Span keyword;
    bool is_assert;
    VecImportAttribute elements;
} ImportAttributes;

void import_attributes_free(ImportAttributes* me); // Null is fine

typedef enum ImportResolutionOverride {
    IMPORT_RESOLUTION_DEFAULT = 0,
    IMPORT_RESOLUTION_IMPORT, // `"resolution-mode": "import"`
    IMPORT_RESOLUTION_REQUIRE,
} ImportResolutionOverride;

// tsc's getResolutionModeOverride(): the resolution mode a type-only import or an import type asks for, reporting what's
// wrong with the attributes of one if errs isn't null (TS1453/1455/1456/1463/1464)
ImportResolutionOverride import_attributes_resolution_mode(ImportAttributes const* me, ErrorCtx* errs);
// A value tsc's isStringLiteral takes: a string literal, not a template
bool import_attribute_value_is_string_literal(Expr const* value);
