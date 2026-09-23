#include "modifier_list.h"
#include "error.h"

// Yes it's just the & operator, but IMO it makes it more readable
#define BITFIELD_OVERLAPS(e, pattern) (bool)((e) & (pattern))

void modifier_list_validate(ModifierList const* me, Span name, ModifierValidationFlags flags, ErrorCtx* errs) {
    bool accessibility_seen = false;
    bool has_modifier_error = false;

// Only shows the first modifier
#define MODIFIER_ERROR(...) \
    if (!has_modifier_error) { error_ctx_error_d(__VA_ARGS__); } \
    modifier_is_error = true; \
    has_modifier_error = true \

    // What each modifier must precede, in the order tsc's checkGrammarModifiers() tests them: an error when one of these
    // came first
    static const int must_precede[MODIFIER_ENUM_CAP][7] = {
        [MODIFIER_OVERRIDE] = { MODIFIER_READONLY, MODIFIER_ACCESSOR, MODIFIER_ASYNC, -1 },
        [MODIFIER_PUBLIC] = {
            MODIFIER_OVERRIDE, MODIFIER_STATIC, MODIFIER_ACCESSOR, MODIFIER_READONLY, MODIFIER_ASYNC, MODIFIER_ABSTRACT, -1,
        },
        [MODIFIER_PRIVATE] = {
            MODIFIER_OVERRIDE, MODIFIER_STATIC, MODIFIER_ACCESSOR, MODIFIER_READONLY, MODIFIER_ASYNC, MODIFIER_ABSTRACT, -1,
        },
        [MODIFIER_PROTECTED] = {
            MODIFIER_OVERRIDE, MODIFIER_STATIC, MODIFIER_ACCESSOR, MODIFIER_READONLY, MODIFIER_ASYNC, MODIFIER_ABSTRACT, -1,
        },
        [MODIFIER_STATIC] = { MODIFIER_READONLY, MODIFIER_ASYNC, MODIFIER_ACCESSOR, MODIFIER_OVERRIDE, -1 },
        [MODIFIER_EXPORT] = { MODIFIER_DECLARE, MODIFIER_ABSTRACT, MODIFIER_ASYNC, -1 },
        [MODIFIER_ABSTRACT] = { MODIFIER_OVERRIDE, MODIFIER_ACCESSOR, -1 },
        [MODIFIER_READONLY] = { -1 }, [MODIFIER_ACCESSOR] = { -1 }, [MODIFIER_EXPORT_DEFAULT] = { -1 },
        [MODIFIER_ASYNC] = { -1 }, [MODIFIER_DECLARE] = { -1 }, [MODIFIER_CONST] = { -1 },
    };

    bool already_seen[MODIFIER_ENUM_CAP] = {0};

    for_each (i, vec_spanned_modifier, &my spanned) {
        intptr_t index = i_index;
        bool modifier_is_error = false;

        if (BITFIELD_OVERLAPS(flags, MODIFIER_VALIDATION_FLAG_MODIFIERS_NOT_ALLOWED)) {
            MODIFIER_ERROR(errs, i->span, 1184, "Modifiers cannot appear here.");
        }

        switch (i->tag) {
            case MODIFIER_READONLY:
                if (!BITFIELD_OVERLAPS(flags, MODIFIER_VALIDATION_FLAG_FIELD)) {
                    MODIFIER_ERROR(errs, i->span, 1024,
                        "'readonly' modifier can only appear on a property declaration or index signature.");
                }
                break;

            case MODIFIER_ABSTRACT:
                if (!BITFIELD_OVERLAPS(flags, MODIFIER_VALIDATION_FLAG_ABSTRACT)) {
                    MODIFIER_ERROR(errs, i->span, 1253, "Abstract properties can only appear within an abstract class.");
                }
                break;

            case MODIFIER_STATIC:
            case MODIFIER_ACCESSOR:
                break;

            case MODIFIER_EXPORT:
            case MODIFIER_EXPORT_DEFAULT:
                if (BITFIELD_OVERLAPS(flags, MODIFIER_VALIDATION_FLAG_FIELD | MODIFIER_VALIDATION_FLAG_METHOD)) {
                    MODIFIER_ERROR(errs, i->span, 1031,
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
                if (BITFIELD_OVERLAPS(flags, MODIFIER_VALIDATION_FLAG_TOPLEVEL_STATEMENT)) {
                    MODIFIER_ERROR(errs, i->span, 1044,
                        "'", i->span, "' modifier cannot appear on a module or namespace element.");
                } else if (accessibility_seen) {
                    MODIFIER_ERROR(errs, i->span, 1028, "Accessibility modifier already seen.");
                }
                accessibility_seen = true;
                break;

            case MODIFIER_CONST:
                if (BITFIELD_OVERLAPS(flags, MODIFIER_VALIDATION_FLAG_FIELD | MODIFIER_VALIDATION_FLAG_METHOD)) {
                    MODIFIER_ERROR(errs, name, 1248, "A class member cannot have the 'const' keyword.");
                }
                break;

            case MODIFIER_ENUM_CAP:
                impossible;
        }

        for (int j = 0; index && must_precede[i->tag][j] >= 0; j++) {
            SpannedModifier const* earlier = nullptr;
            for (intptr_t k = 0; k < index; k++) {
                if ((int)vec_spanned_modifier_at(&my spanned, k)->tag == must_precede[i->tag][j]) {
                    earlier = vec_spanned_modifier_at(&my spanned, k);
                    break;
                }
            }
            if (!earlier) continue;
            MODIFIER_ERROR(errs, i->span, 1029, "'", i->span, "' modifier must precede '", earlier->span, "' modifier.");
            break;
        }

        if (BITFIELD_OVERLAPS(flags, MODIFIER_VALIDATION_FLAG_TYPE_MEMEBER)) {
            switch (i->tag) {
                case MODIFIER_ABSTRACT:
                case MODIFIER_ASYNC:
                case MODIFIER_PRIVATE:
                case MODIFIER_PROTECTED:
                case MODIFIER_PUBLIC:
                    MODIFIER_ERROR(errs, i->span, 1070,
                        "'", i->span, "' modifier cannot appear on a type member.");
                    break;

                default: break;
            }
        }

        if (already_seen[i->tag]) {
            MODIFIER_ERROR(errs, i->span, 1030, "'", i->span, "' modifier already seen.");
        } else if (!modifier_is_error) {
            already_seen[i->tag] = true;
        }
    }

#undef MODIFIER_ERROR
}
