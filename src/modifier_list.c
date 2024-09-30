#include "modifier_list.h"
#include "error.h"

// Yes it's just the & operator, but IMO it makes it more readable
#define BITFIELD_OVERLAPS(e, pattern) (bool)((e) & (pattern))

void modifier_list_validate(ModifierList const* me, Span name, ModifierValidationFlags flags, ErrorCtx* errs) {
    bool accessibility_seen = false;
    bool has_modifier_error = false;

// Only shows the first modifier
#define MODIFIER_ERROR(...) \
    if (!has_modifier_error) { error_ctx_error(__VA_ARGS__); } \
    modifier_is_error = true; \
    has_modifier_error = true \

    // Modifier order:
    // (private|protected|public) (static|accessor) readonly
    static const int modifier_ordering[MODIFIER_ENUM_CAP] = {
        [MODIFIER_READONLY]     = 2,
        [MODIFIER_ABSTRACT]     = 0,
        [MODIFIER_STATIC]       = 1,
        [MODIFIER_ACCESSOR]     = 1,
        [MODIFIER_EXPORT]       = 0,
        [MODIFIER_ASYNC]        = 0,
        [MODIFIER_DECLARE]      = 0,
        [MODIFIER_OVERRIDE]     = 0,
        [MODIFIER_PRIVATE]      = 0,
        [MODIFIER_PROTECTED]    = 0,
        [MODIFIER_PUBLIC]       = 0,
        [MODIFIER_CONST]        = 0,
    };

    bool already_seen[MODIFIER_ENUM_CAP] = {0};

    c_foreach(i, vec_spanned_modifier, my spanned) {
        intptr_t index = i.ref - my spanned.data;
        bool modifier_is_error = false;

        if (BITFIELD_OVERLAPS(flags, MODIFIER_VALIDATION_FLAG_MODIFIERS_NOT_ALLOWED)) {
            MODIFIER_ERROR(errs, i.ref->span, 1184, "Modifiers cannot appear here.");
        }

        switch (i.ref->tag) {
            case MODIFIER_READONLY:
                if (!BITFIELD_OVERLAPS(flags, MODIFIER_VALIDATION_FLAG_FIELD)) {
                    MODIFIER_ERROR(errs, i.ref->span, 1024,
                        "'readonly' modifier can only appear on a property declaration or index signature.");
                }
                break;

            case MODIFIER_ABSTRACT:
                if (!BITFIELD_OVERLAPS(flags, MODIFIER_VALIDATION_FLAG_ABSTRACT)) {
                    MODIFIER_ERROR(errs, i.ref->span, 1253, "Abstract properties can only appear within an abstract class.");
                }
                break;

            case MODIFIER_STATIC:
            case MODIFIER_ACCESSOR:
                break;

            case MODIFIER_EXPORT:
                if (BITFIELD_OVERLAPS(flags, MODIFIER_VALIDATION_FLAG_FIELD | MODIFIER_VALIDATION_FLAG_METHOD)) {
                    MODIFIER_ERROR(errs, i.ref->span, 1031,
                        "'export' modifier cannot appear on class elements of this kind.");
                }
                break;

            case MODIFIER_ASYNC:
            case MODIFIER_DECLARE:
            case MODIFIER_OVERRIDE:
                break;

            case MODIFIER_PRIVATE:
            case MODIFIER_PROTECTED:
            case MODIFIER_PUBLIC:
                if (accessibility_seen) {
                    MODIFIER_ERROR(errs, i.ref->span, 1028, "Accessibility modifier already seen.");
                }
                accessibility_seen = true;
                break;

            case MODIFIER_CONST:
                if (BITFIELD_OVERLAPS(flags, MODIFIER_VALIDATION_FLAG_FIELD | MODIFIER_VALIDATION_FLAG_METHOD)) {
                    MODIFIER_ERROR(errs, name, 1248, "A class member cannot have the 'const' keyword.");
                }
                break;

            case MODIFIER_ENUM_CAP:
                UNREACHABLE;
        }

        if (index &&
            modifier_ordering[vec_spanned_modifier_at(&my spanned, index - 1)->tag] > modifier_ordering[i.ref->tag]
        ) {
            MODIFIER_ERROR(errs, i.ref->span, 1029,
                "'%.*s' modifier must precede '%.*s' modifier.",
                c_SPAN(i.ref->span), c_SPAN(vec_spanned_modifier_at(&my spanned, index - 1)->span));
        }

        if (BITFIELD_OVERLAPS(flags, MODIFIER_VALIDATION_FLAG_TYPE_MEMEBER)) {
            switch (i.ref->tag) {
                case MODIFIER_ABSTRACT:
                case MODIFIER_ASYNC:
                case MODIFIER_PRIVATE:
                case MODIFIER_PROTECTED:
                case MODIFIER_PUBLIC:
                    MODIFIER_ERROR(errs, i.ref->span, 1070,
                        "'%.*s' modifier cannot appear on a type member.", c_SPAN(i.ref->span));
                    break;

                default: break;
            }
        }

        if (already_seen[i.ref->tag]) {
            MODIFIER_ERROR(errs, i.ref->span, 1030, "'%.*s' modifier already seen.", c_SPAN(i.ref->span));
        } else if (!modifier_is_error) {
            already_seen[i.ref->tag] = true;
        }
    }

#undef MODIFIER_ERROR
}
