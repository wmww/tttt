#include "interface_decl.h"
#include "ty/ty.h"
#include "error.h"
#include "interface_member.h"
#include "interface.h"
#include "type.h"
#include "scope.h"
#include "unused.h"
#include "flow.h"
#include "source_file.h"

void interface_decl_drop(InterfaceDecl* me) {
    generic_drop(&my generic);
    vec_spanned_type_drop(&my extends);
    type_drop(&my type);
}

void interface_decl_add_names(InterfaceDecl* me, Scope* scope, DeclExported exported) {
    scope_init_declaration(scope, &my symbol, (DeclProps){
        .name = my name,
        .full = my full_span,
        .exported = exported == DECL_EXPORTED_YES,
        .default_export = exported == DECL_EXPORTED_YES_DEFAULT,
        .kind = SYMBOL_DECL_INTERFACE,
        .node = me,
    });
}

void interface_decl_lookup_names(InterfaceDecl* me, Scope* names, ErrorCtx* errs) {
    unused_register(names, UNUSED_INTERFACE, me, span_null);
    names = scope_new_generic_merged(names, &my generic, my symbol, me, errs);
    Scope* this_scope = scope_new(names, SCOPE_BLOCK);
    this_scope->this_ref = (ThisRef){ .kind = THIS_INTERFACE, .symbol = my symbol, .interface = me };
    names = this_scope;
    check(my type.tag == TYPE_INTERFACE);
    // HACK (tsc's too, for pre-3.0 libs): a `symbol` property of the global `SymbolConstructor` is a unique symbol
    bool symbol_constructor = my symbol && !my symbol->parent && !my symbol->is_local &&
        csview_equals(my symbol->name, "SymbolConstructor");
    for_each (i, vec_interface_member, &my type.interface.members) {
        if (symbol_constructor && i->type.tag == TYPE_SYMBOL) i->type = (TypeNode){ .tag = TYPE_UNIQUE_SYMBOL };
        if (i->type.tag == TYPE_UNIQUE_SYMBOL) i->type.unique_symbol.owner = my symbol;
    }
    for_each (i, vec_spanned_type, &my extends) {
        type_lookup_names(&i->type, names, errs);
    }
    // The body's members directly: it isn't a type literal (whose `this` would be invalid). Computed names in there
    // aren't uses of values (tsc's isInAmbientOrTypeNode)
    bool saved_type_query = names->flow->in_type_query;
    names->flow->in_type_query = true;
    for_each (i, vec_interface_member, &my type.interface.members) {
        interface_member_lookup_names(i, names, errs);
    }
    names->flow->in_type_query = saved_type_query;
}

// Whether the interface itself declares `prop` (rather than inheriting it)
static bool is_own_prop(Prop const* prop, Symbol const* symbol) {
    while (prop->target) prop = prop->target;
    return prop->parent == symbol;
}

typedef struct HeritageBase {
    Ty const* written; // as the extends clause says, for messages
    Ty const* with_this;
} HeritageBase;

// tsc's checkInheritedPropertiesAreIdentical: a property inherited from two bases has to be the same in both (TS2320),
// unless the interface declares its own
static bool inherited_props_identical(
    Checker* checker, Symbol const* symbol, Ty const* type_with_this, HeritageBase const* bases, u32 base_count, Span span,
    ErrorCtx* errs
) {
    if (base_count < 2) return true;
    typedef struct Seen { Prop const* prop; i32 base; } Seen; // base -1: the interface's own
    u32 cap = 0;
    Members const* own = ty_members(checker, type_with_this);
    cap += own->prop_count;
    for (u32 b = 0; b < base_count; b++) cap += ty_members(checker, bases[b].with_this)->prop_count;
    Seen* seen = malloc(sizeof(Seen) * (cap ? cap : 1));
    panic_if(!seen, "out of memory");
    u32 seen_count = 0;
    for (u32 i = 0; i < own->prop_count; i++) {
        if (is_own_prop(own->props[i], symbol)) seen[seen_count++] = (Seen){ own->props[i], -1 };
    }
    bool identical = true;
    for (u32 b = 0; b < base_count; b++) {
        Members const* members = ty_members(checker, bases[b].with_this);
        for (u32 i = 0; i < members->prop_count; i++) {
            Prop const* prop = members->props[i];
            Seen const* existing = nullptr;
            for (u32 j = 0; j < seen_count && !existing; j++) {
                if (seen[j].prop->name == prop->name) existing = &seen[j];
            }
            if (!existing) {
                seen[seen_count++] = (Seen){ prop, (i32)b };
                continue;
            }
            if (existing->base < 0 || ty_props_identical(checker, existing->prop, prop)) continue;
            identical = false;
            cstr type_str = ty_to_cstr(checker, type_with_this);
            cstr first = ty_to_cstr(checker, bases[existing->base].written);
            cstr second = ty_to_cstr(checker, bases[b].written);
            cstr name = ty_prop_name_cstr(checker, prop->name);
            Error* error = error_ctx_error_d(errs, span, 2320, "Interface '", type_str, "' cannot simultaneously extend types '",
                first, "' and '", second, "'.");
            error_sub_error_s(error, span_null, 2319, cstr_d("Named property '", name, "' of types '", first, "' and '", second,
                "' are not identical."));
            c_drop(cstr, &type_str, &first, &second, &name);
        }
    }
    free(seen);
    return identical;
}

// tsc's checkInterfaceDeclaration, once per symbol (at its first declaration): the interface (with its own `this`) has to
// fit every base of every declaration, with that `this`
static void interface_heritage_typecheck(InterfaceDecl* me, ErrorCtx* errs) {
    Symbol const* symbol = my symbol;
    if (!symbol || symbol_find_node(symbol, SYMBOL_DECL_INTERFACE) != me || (symbol->flags & SYMBOL_CLASS)) return;
    Checker* checker = current_checker;
    Ty const* declared = ty_declared_type_of(checker, symbol);
    if (declared->kind != TY_DECLARED) return;
    Ty const* this_param = declared->declared.this_param;
    Ty const* type_with_this = this_param ? ty_with_this_arg(checker, declared, this_param) :
        declared->declared.type_params.len ? ty_reference(checker, declared, declared->declared.type_params) : declared;
    LowerCtx ctx = { .generic = true };
    HeritageBase stack_bases[8];
    HeritageBase* bases = stack_bases;
    u32 base_count = 0, base_cap = c_arraylen(stack_bases);
    for (u32 d = 0; d < symbol->decl_count; d++) {
        if (symbol->decls[d].kind != SYMBOL_DECL_INTERFACE) continue;
        InterfaceDecl const* decl = symbol->decls[d].node;
        for_each (extends, vec_spanned_type, &decl->extends) {
            Ty const* base = ty_from_node(checker, &extends->type, ctx);
            if (!(base->flags & TYF_OBJECT) || base == declared) continue;
            if (base_count == base_cap) {
                base_cap *= 2;
                HeritageBase* grown = malloc(sizeof(HeritageBase) * base_cap);
                panic_if(!grown, "out of memory");
                memcpy(grown, bases, sizeof(HeritageBase) * base_count);
                if (bases != stack_bases) free(bases);
                bases = grown;
            }
            bases[base_count++] = (HeritageBase){ base, this_param ? ty_with_this_arg(checker, base, this_param) : base };
        }
    }
    if (inherited_props_identical(checker, symbol, type_with_this, bases, base_count, my name, errs)) {
        for (u32 b = 0; b < base_count; b++) {
            ty_check_heritage(checker, type_with_this, bases[b].with_this, nullptr, 0, 2430, "Interface", "incorrectly extends interface",
                my name, errs);
        }
        Ty const* written[32];
        u32 written_count = 0;
        for (u32 b = 0; b < base_count && written_count < c_arraylen(written); b++) written[written_count++] = bases[b].written;
        ty_check_index_constraints(checker, declared, symbol, false, written, written_count, my name, errs);
    }
    if (bases != stack_bases) free(bases);
}

void interface_decl_typecheck(InterfaceDecl* me, ErrorCtx* errs) {
    generic_typecheck(&my generic, errs);

    check(my type.tag == TYPE_INTERFACE);
    for_each (extends, vec_spanned_type, &my extends) {
        type_typecheck(&extends->type, errs);
        if (extends->type.tag != TYPE_IDENTIFIER) {
            error_ctx_error_d(
                errs,
                extends->span,
                2499,
                "An interface can only extend an identifier/qualified-name with optional type arguments."
            );
        }
        // TODO: TS2312 (not an object type)
    }

    interface_typecheck(&my type.interface, errs);
    // Once for all declarations of the interface, by the first one that is checked (a lib's aren't)
    InterfaceDecl const* first_checked = nullptr;
    for (u32 d = 0; my symbol && d < my symbol->decl_count && !first_checked; d++) {
        SymbolDecl const* decl = &my symbol->decls[d];
        if (decl->kind == SYMBOL_DECL_INTERFACE && decl->name.source && !decl->name.source->is_stdlib) first_checked = decl->node;
    }
    if (first_checked == me) {
        VecInterfaceMember const* lists[16];
        u32 list_count = 0;
        for (u32 d = 0; d < my symbol->decl_count && list_count < c_arraylen(lists); d++) {
            if (my symbol->decls[d].kind != SYMBOL_DECL_INTERFACE) continue;
            lists[list_count++] = &((InterfaceDecl const*)my symbol->decls[d].node)->type.interface.members;
        }
        // (The declared type first: it makes the later declarations' type parameters the first one's)
        if (list_count > 1) ty_declared_type_of(current_checker, my symbol);
        ty_check_object_type_duplicate_members(current_checker, lists, list_count, errs);
    }
    if (first_checked == me && !(my symbol->flags & SYMBOL_CLASS)) { // A class checks its own
        ty_check_type_parameter_lists_identical(current_checker, my symbol, errs);
    }
    if (first_checked == me) {
        InterfaceMember const* indexes[32];
        u32 index_count = 0;
        for (u32 d = 0; d < my symbol->decl_count; d++) {
            if (my symbol->decls[d].kind != SYMBOL_DECL_INTERFACE) continue;
            InterfaceDecl const* decl = my symbol->decls[d].node;
            for_each (i, vec_interface_member, &decl->type.interface.members) {
                if (i->key.tag == INTERFACE_KEY_INDEX && index_count < c_arraylen(indexes)) indexes[index_count++] = i;
            }
        }
        if (index_count > 1) ty_check_duplicate_index_signatures(current_checker, indexes, index_count, errs);
    }
    // TODO: off until checking `CallChainRoot extends CallChain` early stops making the corpus' aliases circular
    // (triage.md, "TS2430"); TS2415/TS2416/TS2420 for classes are on
    interface_heritage_typecheck(me, errs);
}
