#include "ty/ty_internal.h"
#include "display.h"
#include "program.h"
#include <ctype.h>
#include <time.h>

// `--shadow-types` / TTTT_SHADOW_TYPES=1 (plan 02 stage 1): the legacy checker does all the work as usual, and
// records every type annotation and type declaration it typechecks. Afterwards a Checker lowers all of them to the
// new core and prints them, and the result is compared with what the legacy display makes of the same node wherever
// the two are supposed to agree. Goes away when the new core is the checker (stage 5).

typedef Type const* TypeCPtr;
#define g_key TypeCPtr, type_cptr
#include <cleat/generic/vec.h>

typedef struct ShadowState {
    VecTypeCPtr nodes;
    VecDeclarationCPtr decls;
} ShadowState;

static int enabled = -1; // -1: look at the environment
static ShadowState* recording; // THREADSAFETY: one program typechecks at a time

bool ty_shadow_enabled(void) {
    if (enabled < 0) {
        char const* env = getenv("TTTT_SHADOW_TYPES");
        enabled = env && *env && strcmp(env, "0") != 0;
    }
    return enabled;
}

void ty_shadow_set_enabled(bool value) {
    enabled = value;
}

void ty_shadow_record_node(Type const* node) {
    if (recording) vec_type_cptr_push(&recording->nodes, node);
}

void ty_shadow_record_declaration(Declaration const* decl) {
    if (recording && decl) vec_declaration_cptr_push(&recording->decls, decl);
}

void ty_shadow_begin(Program* program) {
    (void)program;
    if (!ty_shadow_enabled() || recording) return;
    recording = c_new(ShadowState, {0});
}

// Whether the legacy display of a node is supposed to be what the new core prints for it. It isn't wherever the new
// core is deliberately better (aliases print as what they alias unless they name an object/union type, operators
// resolve) or the legacy display shows syntax rather than a type.
static bool node_is_comparable(Type const* node) {
    switch (node->tag) {
        case TYPE_ANY:
        case TYPE_UNKNOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_SYMBOL:
        case TYPE_BIGINT:
        case TYPE_VALUE:
        case TYPE_INFERRED:
            return true;

        case TYPE_IDENTIFIER: {
            IdentifierType const* identifier = node->identifier;
            Declaration const* decl = identifier->decl_ref;
            if (!decl || !decl->type_tag || identifier->keyword || decl->type_tag == DECLARATION_TYPE) return false;
            intptr_t param_count = decl->type_generic ? vec_generic_item_size(&decl->type_generic->items) : 0;
            if (param_count != vec_spanned_type_size(&identifier->generic_params)) return false;
            if (param_count == 1 && (csview_equals(decl->name.sv, "Array") || csview_equals(decl->name.sv, "ReadonlyArray"))) {
                return false; // `Array<T>` prints as `T[]`
            }
            for_each (i, vec_spanned_type, &identifier->generic_params) {
                if (!node_is_comparable(&i->type)) return false;
            }
            return true;
        }

        case TYPE_CALLABLE:
            for_each (i, vec_generic_item, &node->callable->generic.items) {
                if (!node_is_comparable(&i->extends) || !node_is_comparable(&i->fallback)) return false;
            }
            for_each (i, vec_callable_arg, &node->callable->args) {
                if (!i->source || i->source->initializer.binding.tag != BINDING_IDENTIFIER) return false;
                if (!node_is_comparable(&i->type)) return false;
            }
            return node_is_comparable(&node->callable->ret);

        case TYPE_INTERFACE:
            for_each (i, vec_interface_member, &node->interface.members) {
                if (i->key.tag == INTERFACE_KEY_EXPR) return false;
                if (i->key.tag == INTERFACE_KEY_INDEX && !node_is_comparable(&i->key.index.type)) return false;
                if (i->key.tag == INTERFACE_KEY_MAPPED && (
                    !node_is_comparable(&i->key.mapped.in_type) || !node_is_comparable(&i->key.mapped.as_type)
                )) return false;
                if (!node_is_comparable(&i->type)) return false;
            }
            return true;

        case TYPE_ARRAY:
            return node_is_comparable(node->array.element);

        case TYPE_TUPLE:
            for_each (i, vec_tuple_element, &node->tuple.elements) {
                if (i->readonly || !node_is_comparable(&i->type)) return false;
            }
            return true;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            for_each (i, vec_type, &node->composite) {
                if (!node_is_comparable(i)) return false;
            }
            return true;

        case TYPE_INDEXED:
        case TYPE_KEYOF:
        case TYPE_TYPEOF:
        case TYPE_TERNARY:
        case TYPE_LAZY:
        case TYPE_REF:
        case TYPE_NEW:
            return false;
    }
    impossible;
}

static cstr normalise_whitespace(cstr const* s) {
    cstr ret = cstr_null;
    bool pending_space = false;
    c_foreach (i, cstr, *s) {
        if (*i.ref == ' ' || *i.ref == '\n' || *i.ref == '\t') {
            pending_space = true;
        } else {
            if (pending_space && !cstr_is_empty(&ret)) cstr_append_lit(&ret, " ");
            pending_space = false;
            cstr_append_sv(&ret, i.chr);
        }
    }
    return ret;
}

// Whether two renderings are the same ` | ` separated parts in a different order. The legacy display keeps the
// written order and duplicates; the new core prints in type id order like tsc.
static int cmp_sv(void const* a, void const* b) { return csview_cmp((csview const*)a, (csview const*)b); }
static bool same_union_members(csview a, csview b) {
    csview parts[2][64];
    u32 counts[2] = {0};
    csview inputs[2] = { a, b };
    for (u32 side = 0; side < 2; side++) {
        isize_t pos = 0;
        while (pos <= inputs[side].size) {
            csview part = csview_token(inputs[side], " | ", &pos);
            if (counts[side] == 64) return false;
            parts[side][counts[side]++] = part;
        }
        qsort(parts[side], counts[side], sizeof(csview), cmp_sv);
        u32 unique = 0;
        for (u32 i = 0; i < counts[side]; i++) {
            if (!unique || !csview_eq(&parts[side][unique - 1], &parts[side][i])) parts[side][unique++] = parts[side][i];
        }
        counts[side] = unique;
    }
    if (counts[0] != counts[1] || counts[0] < 2) return false;
    for (u32 i = 0; i < counts[0]; i++) {
        if (!csview_eq(&parts[0][i], &parts[1][i])) return false;
    }
    return true;
}

// Drops every `Name.` qualifier. The legacy display qualifies names the way they were written, the new core always
// does (see qualified_name()), and tsc does something in between
static cstr strip_qualifiers(cstr const* s) {
    cstr ret = cstr_null;
    csview sv = cstr_sv(s);
    isize_t i = 0;
    while (i < sv.size) {
        isize_t start = i;
        bool is_name = isalpha((unsigned char)sv.buf[i]) || sv.buf[i] == '_' || sv.buf[i] == '$';
        if (is_name && (start == 0 || !isalnum((unsigned char)sv.buf[start - 1]))) {
            while (i < sv.size && (isalnum((unsigned char)sv.buf[i]) || sv.buf[i] == '_' || sv.buf[i] == '$')) i++;
            if (i + 1 < sv.size && sv.buf[i] == '.' && sv.buf[i + 1] != '.') {
                i++;
                continue;
            }
        } else {
            i++;
        }
        cstr_append_sv(&ret, csview_from_n(sv.buf + start, i - start));
    }
    return ret;
}

static char const* tag_name(Type const* node) {
    switch (node->tag) {
        case TYPE_VALUE: return "literal";
        case TYPE_IDENTIFIER: return "identifier";
        case TYPE_CALLABLE: return "function type";
        case TYPE_INTERFACE: return "type literal";
        case TYPE_ARRAY: return "array";
        case TYPE_TUPLE: return "tuple";
        case TYPE_UNION: return "union";
        case TYPE_INTERSECTION: return "intersection";
        default: return "other";
    }
}

void ty_shadow_finish(Program* program) {
    if (!recording) return;
    ShadowState* state = recording;
    recording = nullptr;

    struct timespec start_time;
    timespec_get(&start_time, TIME_UTC); // clock_gettime() is hidden by -std=c2x
    Checker* checker = program_get_checker(program);
    u32 compared = 0, mismatches = 0, reordered = 0, requalified = 0;
    bool dump = getenv("TTTT_SHADOW_DUMP"); // Every recorded node's type id and rendering, for probing by hand

    for_each (i, vec_declaration_cptr, &state->decls) {
        if ((*i)->type_tag) {
            Ty const* ty = ty_declared_type_of(checker, *i);
            // Members of everything declared get resolved and printed too, that's where most of the machinery is
            cstr rendered = cstr_null;
            TyDisplay d = { .target = &rendered };
            Members const* members = ty_members(checker, ty);
            for (u32 j = 0; j < members->prop_count; j++) {
                ty_display(checker, d, prop_type(checker, members->props[j]));
            }
            ty_display(checker, d, ty);
            cstr_drop(&rendered);
        }
    }

    for_each (i, vec_type_cptr, &state->nodes) {
        Type const* node = *i;
        // Everything recorded is lowered as if no type parameter was in scope except the ones it declares itself,
        // which is only right for printing
        Ty const* ty = ty_from_node(checker, node, (LowerCtx){ .generic = true });
        cstr rendered = ty_to_cstr(checker, ty);
        if (dump) {
            log_info("shadow-types: #", (u64)ty->id, " ", cstr_sv(&rendered));
        }
        if (!ty->alias && node_is_comparable(node)) {
            compared++;
            cstr legacy_raw = cstr_null;
            display_type((Display){ .target = &legacy_raw }, node);
            cstr legacy = normalise_whitespace(&legacy_raw);
            cstr_replace(&legacy, ";}", "; }"); // A quirk of the legacy display of nested type literals
            cstr rendered_raw = rendered;
            rendered = normalise_whitespace(&rendered_raw);
            cstr_drop(&rendered_raw);
            if (!cstr_eq(&legacy, &rendered)) {
                cstr legacy_stripped = strip_qualifiers(&legacy);
                cstr rendered_stripped = strip_qualifiers(&rendered);
                bool qualification_only = cstr_eq(&legacy_stripped, &rendered_stripped);
                cstr_drop(&legacy_stripped);
                cstr_drop(&rendered_stripped);
                if (qualification_only) {
                    requalified++;
                } else if (same_union_members(cstr_sv(&legacy), cstr_sv(&rendered))) {
                    reordered++;
                } else {
                    mismatches++;
                    log_error(
                        "shadow-types mismatch (", tag_name(node), "): legacy '", cstr_sv(&legacy),
                        "' new '", cstr_sv(&rendered), "'"
                    );
                }
            }
            cstr_drop(&legacy_raw);
            cstr_drop(&legacy);
        }
        cstr_drop(&rendered);
    }

    if (getenv("TTTT_SHADOW_STATS")) {
        struct timespec end_time;
        timespec_get(&end_time, TIME_UTC);
        u64 micros = (u64)(end_time.tv_sec - start_time.tv_sec) * 1000000 +
            (u64)((end_time.tv_nsec - start_time.tv_nsec) / 1000);
        log_info(
            "shadow-types: nodes ", (u64)vec_type_cptr_size(&state->nodes),
            ", declarations ", (u64)vec_declaration_cptr_size(&state->decls),
            ", compared ", (u64)compared, ", mismatches ", (u64)mismatches, ", union order only ", (u64)reordered,
            ", qualification only ", (u64)requalified,
            ", types ", (u64)(checker->next_type_id - 1), ", arena bytes ", (u64)checker->arena.total,
            ", microseconds ", micros,
            checker->instantiation_limit_hit ? ", instantiation limit hit" : ""
        );
    }

    vec_type_cptr_drop(&state->nodes);
    vec_declaration_cptr_drop(&state->decls);
    c_free_n(state, 1);
}
