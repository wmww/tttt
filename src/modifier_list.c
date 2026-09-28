#include "modifier_list.h"
#include "error.h"

#define HAS(tag) (seen & (1u << (tag)))

// tsc's findFirstIllegalModifier(): modifiers that are never allowed on this kind of node, "Modifiers cannot appear here"
static SpannedModifier const* first_illegal_modifier(ModifierList const* me, ModifiedNode const* node) {
    SpannedModifier const* first = vec_spanned_modifier_front(&my spanned);
    switch (node->kind) {
        case MODIFIED_GET_ACCESSOR: case MODIFIED_SET_ACCESSOR: case MODIFIED_CONSTRUCTOR: case MODIFIED_PROPERTY:
        case MODIFIED_PROPERTY_SIGNATURE: case MODIFIED_METHOD: case MODIFIED_METHOD_SIGNATURE:
        case MODIFIED_INDEX_SIGNATURE: case MODIFIED_MODULE: case MODIFIED_IMPORT: case MODIFIED_IMPORT_EQUALS:
        case MODIFIED_EXPORT_DECLARATION: case MODIFIED_EXPORT_ASSIGNMENT: case MODIFIED_PARAMETER:
        case MODIFIED_TYPE_PARAMETER:
            return nullptr;
        case MODIFIED_CLASS_STATIC_BLOCK: case MODIFIED_NAMESPACE_EXPORT: case MODIFIED_MISSING_DECLARATION:
            return first;
        default: break;
    }
    if (node->parent == MODIFIED_IN_SOURCE_FILE || node->parent == MODIFIED_IN_MODULE_BLOCK) return nullptr;
    switch (node->kind) {
        case MODIFIED_FUNCTION: return first->tag == MODIFIER_ASYNC ? nullptr : first;
        case MODIFIED_CLASS: return first->tag == MODIFIER_ABSTRACT ? nullptr : first;
        case MODIFIED_ENUM: return first->tag == MODIFIER_CONST ? nullptr : first;
        default: return first;
    }
}

static bool is_class_element(ModifiedKind kind) {
    return kind == MODIFIED_PROPERTY || kind == MODIFIED_METHOD || kind == MODIFIED_GET_ACCESSOR ||
        kind == MODIFIED_SET_ACCESSOR;
}

bool modifier_list_check_grammar(ModifierList const* me, ModifiedNode const* node, ErrorCtx* errs) {
    if (vec_spanned_modifier_is_empty(&my spanned)) return false;
#define REPORT(span, ...) do { error_ctx_error_d(errs, span, __VA_ARGS__); return true; } while (0)
#define MUST_PRECEDE(a, b) REPORT(s, 1029, "'", a, "' modifier must precede '", b, "' modifier.")
#define CANNOT_BE_USED_WITH(a, b) REPORT(s, 1243, "'", a, "' modifier cannot be used with '", b, "' modifier.")
#define ALREADY_SEEN() REPORT(s, 1030, "'", s, "' modifier already seen.")

    SpannedModifier const* illegal = first_illegal_modifier(me, node);
    if (illegal) REPORT(illegal->span, 1184, "Modifiers cannot appear here.");

    u32 seen = 0;
    const u32 accessibility = (1u << MODIFIER_PUBLIC) | (1u << MODIFIER_PROTECTED) | (1u << MODIFIER_PRIVATE);
    Span last_static = span_null, last_declare = span_null, last_async = span_null, last_override = span_null;
    bool in_module = node->parent == MODIFIED_IN_SOURCE_FILE || node->parent == MODIFIED_IN_MODULE_BLOCK;
    for_each (m, vec_spanned_modifier, &my spanned) {
        Span s = m->span;
        if (m->tag != MODIFIER_READONLY) {
            if (node->kind == MODIFIED_PROPERTY_SIGNATURE || node->kind == MODIFIED_METHOD_SIGNATURE) {
                REPORT(s, 1070, "'", s, "' modifier cannot appear on a type member.");
            }
            if (node->kind == MODIFIED_INDEX_SIGNATURE &&
                (m->tag != MODIFIER_STATIC || node->parent != MODIFIED_IN_CLASS)
            ) {
                REPORT(s, 1071, "'", s, "' modifier cannot appear on an index signature.");
            }
        }
        if (m->tag != MODIFIER_IN && m->tag != MODIFIER_OUT && m->tag != MODIFIER_CONST && node->kind == MODIFIED_TYPE_PARAMETER) {
            REPORT(s, 1273, "'", s, "' modifier cannot appear on a type parameter");
        }
        switch (m->tag) {
            case MODIFIER_CONST:
                if (node->kind != MODIFIED_ENUM && node->kind != MODIFIED_TYPE_PARAMETER) {
                    REPORT(node->name, 1248, "A class member cannot have the 'const' keyword.");
                }
                if (node->kind == MODIFIED_TYPE_PARAMETER && node->type_param_owner == TYPE_PARAM_OF_TYPE) {
                    REPORT(s, 1277, "'const' modifier can only appear on a type parameter of a function, method or class");
                }
                break;

            case MODIFIER_IN:
            case MODIFIER_OUT:
                if (node->kind != MODIFIED_TYPE_PARAMETER || node->type_param_owner == TYPE_PARAM_OF_FUNCTION) {
                    REPORT(s, 1274, "'", s, "' modifier can only appear on a type parameter of a class, interface or type alias");
                }
                if (HAS(m->tag)) ALREADY_SEEN();
                if (m->tag == MODIFIER_IN && HAS(MODIFIER_OUT)) MUST_PRECEDE("in", "out");
                break;

            case MODIFIER_OVERRIDE:
                if (HAS(MODIFIER_OVERRIDE)) ALREADY_SEEN();
                if (HAS(MODIFIER_DECLARE)) CANNOT_BE_USED_WITH("override", "declare");
                if (HAS(MODIFIER_READONLY)) MUST_PRECEDE("override", "readonly");
                if (HAS(MODIFIER_ACCESSOR)) MUST_PRECEDE("override", "accessor");
                if (HAS(MODIFIER_ASYNC)) MUST_PRECEDE("override", "async");
                last_override = s;
                break;

            case MODIFIER_PUBLIC:
            case MODIFIER_PROTECTED:
            case MODIFIER_PRIVATE:
                if (seen & accessibility) REPORT(s, 1028, "Accessibility modifier already seen.");
                if (HAS(MODIFIER_OVERRIDE)) MUST_PRECEDE(s, "override");
                if (HAS(MODIFIER_STATIC)) MUST_PRECEDE(s, "static");
                if (HAS(MODIFIER_ACCESSOR)) MUST_PRECEDE(s, "accessor");
                if (HAS(MODIFIER_READONLY)) MUST_PRECEDE(s, "readonly");
                if (HAS(MODIFIER_ASYNC)) MUST_PRECEDE(s, "async");
                if (in_module) REPORT(s, 1044, "'", s, "' modifier cannot appear on a module or namespace element.");
                if (HAS(MODIFIER_ABSTRACT)) {
                    if (m->tag == MODIFIER_PRIVATE) CANNOT_BE_USED_WITH(s, "abstract");
                    MUST_PRECEDE(s, "abstract");
                }
                if (node->private_name && is_class_element(node->kind)) {
                    REPORT(s, 18010, "An accessibility modifier cannot be used with a private identifier.");
                }
                break;

            case MODIFIER_STATIC:
                if (HAS(MODIFIER_STATIC)) ALREADY_SEEN();
                if (HAS(MODIFIER_READONLY)) MUST_PRECEDE("static", "readonly");
                if (HAS(MODIFIER_ASYNC)) MUST_PRECEDE("static", "async");
                if (HAS(MODIFIER_ACCESSOR)) MUST_PRECEDE("static", "accessor");
                if (in_module) REPORT(s, 1044, "'static' modifier cannot appear on a module or namespace element.");
                if (node->kind == MODIFIED_PARAMETER) REPORT(s, 1090, "'static' modifier cannot appear on a parameter.");
                if (HAS(MODIFIER_ABSTRACT)) CANNOT_BE_USED_WITH("static", "abstract");
                if (HAS(MODIFIER_OVERRIDE)) MUST_PRECEDE("static", "override");
                last_static = s;
                break;

            case MODIFIER_ACCESSOR:
                if (HAS(MODIFIER_ACCESSOR)) ALREADY_SEEN();
                if (HAS(MODIFIER_READONLY)) CANNOT_BE_USED_WITH("accessor", "readonly");
                if (HAS(MODIFIER_DECLARE)) CANNOT_BE_USED_WITH("accessor", "declare");
                if (node->kind != MODIFIED_PROPERTY) {
                    REPORT(s, 1275, "'accessor' modifier can only appear on a property declaration.");
                }
                break;

            case MODIFIER_READONLY:
                if (HAS(MODIFIER_READONLY)) ALREADY_SEEN();
                if (node->kind != MODIFIED_PROPERTY && node->kind != MODIFIED_PROPERTY_SIGNATURE &&
                    node->kind != MODIFIED_INDEX_SIGNATURE && node->kind != MODIFIED_PARAMETER
                ) {
                    REPORT(s, 1024, "'readonly' modifier can only appear on a property declaration or index signature.");
                }
                if (HAS(MODIFIER_ACCESSOR)) CANNOT_BE_USED_WITH("readonly", "accessor");
                break;

            case MODIFIER_EXPORT:
                if (HAS(MODIFIER_EXPORT)) ALREADY_SEEN();
                if (HAS(MODIFIER_DECLARE)) MUST_PRECEDE("export", "declare");
                if (HAS(MODIFIER_ABSTRACT)) MUST_PRECEDE("export", "abstract");
                if (HAS(MODIFIER_ASYNC)) MUST_PRECEDE("export", "async");
                if (node->parent == MODIFIED_IN_CLASS) {
                    REPORT(s, 1031, "'export' modifier cannot appear on class elements of this kind.");
                }
                if (node->kind == MODIFIED_PARAMETER) REPORT(s, 1090, "'export' modifier cannot appear on a parameter.");
                if (node->using_kind == 1) REPORT(s, 1491, "'export' modifier cannot appear on a 'using' declaration.");
                if (node->using_kind == 2) {
                    REPORT(s, 1495, "'export' modifier cannot appear on an 'await using' declaration.");
                }
                break;

            case MODIFIER_EXPORT_DEFAULT:
                if (node->parent == MODIFIED_IN_MODULE_BLOCK && node->parent_is_namespace) {
                    REPORT(s, 1319, "A default export can only be used in an ECMAScript-style module.");
                }
                if (node->using_kind == 1) REPORT(s, 1491, "'default' modifier cannot appear on a 'using' declaration.");
                if (node->using_kind == 2) {
                    REPORT(s, 1495, "'default' modifier cannot appear on an 'await using' declaration.");
                }
                if (!HAS(MODIFIER_EXPORT)) MUST_PRECEDE("export", "default");
                break;

            case MODIFIER_DECLARE:
                if (HAS(MODIFIER_DECLARE)) ALREADY_SEEN();
                if (HAS(MODIFIER_ASYNC)) REPORT(s, 1040, "'async' modifier cannot be used in an ambient context.");
                if (HAS(MODIFIER_OVERRIDE)) REPORT(s, 1040, "'override' modifier cannot be used in an ambient context.");
                if (node->parent == MODIFIED_IN_CLASS && node->kind != MODIFIED_PROPERTY) {
                    REPORT(s, 1031, "'declare' modifier cannot appear on class elements of this kind.");
                }
                if (node->kind == MODIFIED_PARAMETER) REPORT(s, 1090, "'declare' modifier cannot appear on a parameter.");
                if (node->using_kind == 1) REPORT(s, 1491, "'declare' modifier cannot appear on a 'using' declaration.");
                if (node->using_kind == 2) {
                    REPORT(s, 1495, "'declare' modifier cannot appear on an 'await using' declaration.");
                }
                if (node->parent_ambient && node->parent == MODIFIED_IN_MODULE_BLOCK) {
                    REPORT(s, 1038, "A 'declare' modifier cannot be used in an already ambient context.");
                }
                if (node->private_name && is_class_element(node->kind)) {
                    REPORT(s, 18019, "'declare' modifier cannot be used with a private identifier.");
                }
                if (HAS(MODIFIER_ACCESSOR)) CANNOT_BE_USED_WITH("declare", "accessor");
                last_declare = s;
                break;

            case MODIFIER_ABSTRACT:
                if (HAS(MODIFIER_ABSTRACT)) ALREADY_SEEN();
                if (node->kind != MODIFIED_CLASS) {
                    if (node->kind != MODIFIED_METHOD && node->kind != MODIFIED_PROPERTY &&
                        node->kind != MODIFIED_GET_ACCESSOR && node->kind != MODIFIED_SET_ACCESSOR
                    ) {
                        REPORT(s, 1242, "'abstract' modifier can only appear on a class, method, or property declaration.");
                    }
                    if (!(node->parent == MODIFIED_IN_CLASS && node->in_abstract_class)) {
                        if (node->kind == MODIFIED_PROPERTY) {
                            REPORT(s, 1253, "Abstract properties can only appear within an abstract class.");
                        }
                        REPORT(s, 1244, "Abstract methods can only appear within an abstract class.");
                    }
                    if (HAS(MODIFIER_STATIC)) CANNOT_BE_USED_WITH("static", "abstract");
                    if (HAS(MODIFIER_PRIVATE)) CANNOT_BE_USED_WITH("private", "abstract");
                    if (HAS(MODIFIER_ASYNC)) {
                        REPORT(last_async, 1243, "'async' modifier cannot be used with 'abstract' modifier.");
                    }
                    if (HAS(MODIFIER_OVERRIDE)) MUST_PRECEDE("abstract", "override");
                    if (HAS(MODIFIER_ACCESSOR)) MUST_PRECEDE("abstract", "accessor");
                }
                if (node->private_name) REPORT(s, 18019, "'abstract' modifier cannot be used with a private identifier.");
                break;

            case MODIFIER_ASYNC:
                if (HAS(MODIFIER_ASYNC)) ALREADY_SEEN();
                if (HAS(MODIFIER_DECLARE) || node->parent_ambient) {
                    REPORT(s, 1040, "'async' modifier cannot be used in an ambient context.");
                }
                if (node->kind == MODIFIED_PARAMETER) REPORT(s, 1090, "'async' modifier cannot appear on a parameter.");
                if (HAS(MODIFIER_ABSTRACT)) CANNOT_BE_USED_WITH("async", "abstract");
                last_async = s;
                break;

            case MODIFIER_ENUM_CAP:
                impossible;
        }
        seen |= 1u << m->tag;
    }

    if (node->kind == MODIFIED_CONSTRUCTOR) {
        if (HAS(MODIFIER_STATIC)) REPORT(last_static, 1089, "'static' modifier cannot appear on a constructor declaration.");
        if (HAS(MODIFIER_OVERRIDE)) {
            REPORT(last_override, 1089, "'override' modifier cannot appear on a constructor declaration.");
        }
        if (HAS(MODIFIER_ASYNC)) REPORT(last_async, 1089, "'async' modifier cannot appear on a constructor declaration.");
        return false;
    }
    if ((node->kind == MODIFIED_IMPORT || node->kind == MODIFIED_IMPORT_EQUALS) && HAS(MODIFIER_DECLARE)) {
        REPORT(last_declare, 1079, "A 'declare' modifier cannot be used with an import declaration.");
    }
    // (A parameter property with a binding pattern or a rest parameter is function_impl_typecheck()'s)
    if (HAS(MODIFIER_ASYNC) && node->kind != MODIFIED_METHOD && node->kind != MODIFIED_FUNCTION) {
        // tsc's checkGrammarAsyncModifier()
        REPORT(last_async, 1042, "'async' modifier cannot be used here.");
    }
    return false;
#undef REPORT
#undef MUST_PRECEDE
#undef CANNOT_BE_USED_WITH
#undef ALREADY_SEEN
}

#undef HAS
