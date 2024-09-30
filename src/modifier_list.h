#pragma once

#include "span.h"
typedef struct ErrorCtx ErrorCtx;
typedef struct vec_error VecError;

typedef struct SpannedModifier {
    enum {
        MODIFIER_READONLY,
        MODIFIER_ABSTRACT,
        MODIFIER_STATIC,
        MODIFIER_ACCESSOR,
        MODIFIER_EXPORT,
        MODIFIER_ASYNC,
        MODIFIER_DECLARE,
        MODIFIER_OVERRIDE,
        MODIFIER_PRIVATE,
        MODIFIER_PROTECTED,
        MODIFIER_PUBLIC,
        MODIFIER_CONST,
        MODIFIER_ENUM_CAP,
    } tag;
    Span span;
} SpannedModifier;

#define i_key_names SpannedModifier, spanned_modifier
#include "wrap_stc/vec.h"

typedef struct ModifierList {
    VecSpannedModifier spanned;
    bool includes[MODIFIER_ENUM_CAP];
} ModifierList;

static void modifier_list_drop(ModifierList* me) {
    vec_spanned_modifier_drop(&my spanned);
}

static ModifierList modifier_list_clone(ModifierList me) {
    me.spanned = vec_spanned_modifier_clone(me.spanned);
    return me;
}

typedef enum {
    MODIFIER_VALIDATION_FLAG_MODIFIERS_NOT_ALLOWED  = 1 << 0,
    MODIFIER_VALIDATION_FLAG_TYPE_MEMEBER           = 1 << 1,
    MODIFIER_VALIDATION_FLAG_ABSTRACT               = 1 << 2,
    MODIFIER_VALIDATION_FLAG_FIELD                  = 1 << 3,
    MODIFIER_VALIDATION_FLAG_METHOD                 = 1 << 4,
    MODIFIER_VALIDATION_FLAG_CLASS_STATIC_BLOCK     = 1 << 5,
} ModifierValidationFlags;

void modifier_list_validate(ModifierList const* me, Span name, ModifierValidationFlags flags, ErrorCtx* errs);
