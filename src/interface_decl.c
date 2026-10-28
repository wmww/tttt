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
#include "expr.h"

void interface_decl_drop(InterfaceDecl* me) {
    generic_drop(&my generic);
    vec_spanned_type_drop(&my extends);
    vec_type_drop(&my unchecked_extends);
    vec_expr_drop(&my invalid_extends);
    type_drop(&my type);
}

void interface_decl_add_names(InterfaceDecl* me, Scope* scope, DeclStatementProps props) {
    scope_init_declaration(scope, &my symbol, (DeclProps){
        .name = my name,
        .full = my full_span,
        .exported = props.exported == DECL_EXPORTED_YES,
        .default_export = props.exported == DECL_EXPORTED_YES_DEFAULT,
        .declare_keyword = span_valid(props.declare_keyword),
        .kind = SYMBOL_DECL_INTERFACE,
        .node = me,
    });
}

void interface_decl_lookup_names(InterfaceDecl* me, Scope* names, ErrorCtx* errs) {
    unused_register(names, UNUSED_INTERFACE, me);
    my outer_generics = names->generics;
    names = scope_new_generic_merged(names, &my generic, my symbol, me, my name, errs);
    ScopePendingMark pending_mark = scope_pending_mark(names->binder);
    Scope* this_scope = scope_new(names, SCOPE_BLOCK);
    scope_rare_mut(this_scope)->this_ref = (ThisRef){ .kind = THIS_INTERFACE, .symbol = my symbol, .interface = me };
    names = this_scope;
    check(my type.tag == TYPE_INTERFACE);
    // (Its nodes know which interface they are in: tsc's isTypeParameterPossiblyReferenced, TypeFlow.owner)
    TypeFlow const* saved_flow = type_flow_enter_owner(my symbol, binder_arena(names->binder));
    // HACK (tsc's too, for pre-3.0 libs): a `symbol` property of the global `SymbolConstructor` is a unique symbol
    bool symbol_constructor = my symbol && !my symbol->parent && !my symbol->is_local &&
        csview_equals(my symbol->name, "SymbolConstructor");
    for_each (i, vec_interface_member, &my type.interface.members) {
        // (Only a readonly one, tsc's getESSymbolLikeTypeForNode: isValidESSymbolDeclaration)
        if (symbol_constructor && i->type.tag == TYPE_SYMBOL && i->readonly) {
            i->type.tag = TYPE_UNIQUE_SYMBOL;
            i->type.unique_symbol = (UniqueSymbolDecl){0};
            i->type.written_symbol = true;
        }
        if (i->type.tag == TYPE_UNIQUE_SYMBOL) i->type.unique_symbol.owner = my symbol;
    }
    for_each (i, vec_spanned_type, &my extends) {
        type_lookup_names(&i->type, names, errs);
    }
    // (What tsc never checks: a later `extends` clause, expressions that aren't names past TS2499: no uses, nothing
    // unused)
    unused_marking_suppressed++;
    for_each (i, vec_type, &my unchecked_extends) type_lookup_names(i, names, nullptr);
    for_each (i, vec_expr, &my invalid_extends) expr_lookup_names(i, names, nullptr);
    unused_marking_suppressed--;
    // The body's members directly: it isn't a type literal (whose `this` would be invalid). Computed names in there
    // aren't uses of values (tsc's isInAmbientOrTypeNode)
    bool saved_type_query = scope_flow(names)->in_type_query;
    scope_flow(names)->in_type_query = true;
    for_each (i, vec_interface_member, &my type.interface.members) {
        interface_member_lookup_names(i, names, errs);
    }
    scope_flow(names)->in_type_query = saved_type_query;
    type_flow_restore(saved_flow);
    scope_release(this_scope, pending_mark);
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
        if (is_own_prop(members_prop(checker, own, i), symbol)) seen[seen_count++] = (Seen){ members_prop(checker, own, i), -1 };
    }
    bool identical = true;
    for (u32 b = 0; b < base_count; b++) {
        Members const* members = ty_members(checker, bases[b].with_this);
        for (u32 i = 0; i < members->prop_count; i++) {
            Prop const* prop = members_prop(checker, members, i);
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

// Whether the program walks the file a declaration is in: a library file only when lib files are checked (the test
// harness shares them between programs with other options)
static bool decl_file_walked(Span name) {
    SourceFile const* file = span_source(name);
    if (!file) return false;
    return file->is_stdlib ? lib_files_checked() : source_file_is_checked(file);
}

// tsc's checkInterfaceDeclaration, once per symbol, by the first of its declarations that is checked (a user one when
// the first is a library's that isn't), reported at its first declaration: the interface (with its own `this`) has to
// fit every base of every declaration, with that `this`. (What it finds in an unchecked library file goes unreported)
static void interface_heritage_typecheck(InterfaceDecl* me, ErrorCtx* errs) {
    Symbol const* symbol = my symbol;
    if (!symbol) return;
    // (A symbol augmentations cloned along a chain longer than one means its end, but its own declarations are checked
    // as its first clone: tsc's getSymbolOfDeclaration applies getMergedSymbol once, and checkInterfaceDeclaration only
    // checks at the first declaration. `declare module 'vitest' { interface Assertion extends CustomMatchers }` over
    // @vitest/expect's Assertion, which vitest augmented first, isn't a base there: no TS2320 on @vitest/expect's)
    // HACK (hacks.md): the one place the first clone is used; types, names and symbols still see the end of the chain
    if (symbol_rare(symbol)->aug_one_level) symbol = symbol_rare(symbol)->aug_one_level;
    InterfaceDecl const* first = symbol_find_node(symbol, SYMBOL_DECL_INTERFACE);
    if (!first) return;
    for (u32 d = 0; d < symbol->decl_count; d++) {
        SymbolDecl const* decl = &symbol->decls[d];
        if (decl->kind != SYMBOL_DECL_INTERFACE || !decl_file_walked(decl->name)) continue;
        if (decl->node != me) return;
        break;
    }
    Span name = first->name;
    Checker* checker = current_checker;
    Ty const* declared = ty_declared_type_of(checker, symbol);
    if (declared->kind != TY_DECLARED) return;
    if (ty_interface_in_base_cycle(checker, symbol)) return; // It has no bases (TS2310)
    Ty const* this_param = declared->declared.this_param;
    Ty const* type_with_this = this_param ? ty_with_this_arg(checker, declared, this_param) :
        declared->declared.type_params.len ? ty_reference(checker, declared, declared->declared.type_params) : declared;
    LowerCtx ctx = { .generic = true };
    HeritageBase stack_bases[8];
    HeritageBase* bases = stack_bases;
    u32 base_count = 0, base_cap = c_arraylen(stack_bases);
    // (Merged with a class: its base comes first, tsc's getBaseTypes: resolveBaseTypesOfClass, then the interfaces')
    ClassDecl const* class_decl = symbol->flags & SYMBOL_CLASS ? symbol_find_node(symbol, SYMBOL_DECL_CLASS) : nullptr;
    Ty const* class_base = class_decl ? ty_class_base_instance(checker, class_decl) : nullptr;
    if (class_base && (class_base->flags & TYF_OBJECT)) {
        bases[base_count++] = (HeritageBase){ class_base, this_param ? ty_with_this_arg(checker, class_base, this_param) : class_base };
    }
    for (u32 d = 0; d < symbol->decl_count; d++) {
        if (symbol->decls[d].kind != SYMBOL_DECL_INTERFACE) continue;
        InterfaceDecl const* decl = symbol->decls[d].node;
        for_each (extends, vec_spanned_type, &decl->extends) {
            Ty const* base = ty_from_node(checker, &extends->type, ctx);
            // (Not a keyword, TS2840/TS2499 there)
            if (extends->type.tag == TYPE_IDENTIFIER && !ty_is_valid_base_type(checker, base)) {
                error_ctx_error_d(errs, extends->span, 2312, "An interface can only extend an object type or intersection of "
                    "object types with statically known members.");
                    continue;
            }
            // (An object type, or a type parameter: `interface I<T extends object> extends T`)
            // (An intersection of object types too: tsc's resolveBaseTypesOfInterface takes isValidBaseType ones)
            if (!(base->flags & TYF_OBJECT) && base->kind != TY_TYPE_PARAM && base->kind != TY_INTERSECTION) continue;
            if (base == declared) continue;
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
    if (inherited_props_identical(checker, symbol, type_with_this, bases, base_count, name, errs)) {
        for (u32 b = 0; b < base_count; b++) {
            ty_check_heritage(checker, type_with_this, bases[b].with_this, nullptr, 0, 2430, "Interface", "incorrectly extends interface",
                name, errs);
        }
        Ty const* written[32];
        u32 written_count = 0;
        for (u32 b = 0; b < base_count && written_count < c_arraylen(written); b++) written[written_count++] = bases[b].written;
        ty_check_index_constraints(checker, declared, symbol, false, written, written_count, name, errs);
    }
    if (bases != stack_bases) free(bases);
}

void interface_decl_typecheck(InterfaceDecl* me, ErrorCtx* errs) {
    generic_typecheck(&my generic, errs);
    ty_check_variance_annotations(current_checker, my symbol, &my generic, false, errs);
    // tsc's getBaseTypes() reports the declarations of an interface whose bases lead back to it (resolving the bases
    // first: reducing one can find the cycle, `interface Y extends X<Y>` with a mapped type over `keyof Y` in X)
    if (my symbol && span_valid(my name) && !span_source(my name)->is_stdlib) ty_members(current_checker, ty_declared_type_of(current_checker, my symbol));
    if (my symbol && span_valid(my name) && !span_source(my name)->is_stdlib && (ty_bases_reentered(current_checker, my symbol) ||
        (ty_interface_in_base_cycle(current_checker, my symbol) && ty_interface_decl_reports_base_cycle(current_checker, my symbol, me)))
    ) {
        Ty const* declared = ty_declared_type_of(current_checker, my symbol);
        if (declared->kind == TY_DECLARED && declared->declared.type_params.len) {
            declared = ty_reference(current_checker, declared, declared->declared.type_params);
        }
        error_ctx_error_d(errs, my name, 2310, "Type '", declared, "' recursively references itself as a base type.");
    }

    // The bases and index signatures are checked before the heritage clauses and members are walked (tsc's
    // checkInterfaceDeclaration, its first lazy diagnostic)
    interface_heritage_typecheck(me, errs);
    check(my type.tag == TYPE_INTERFACE);
    for_each (extends, vec_spanned_type, &my extends) {
        type_typecheck(&extends->type, errs);
        if (extends->type.tag != TYPE_IDENTIFIER && is_primitive_type_name(span_sv(extends->span))) {
            // (A keyword type: what the parser made of an identifier in an expression's place, tsc's
            // checkAndReportErrorForUsingTypeAsValue)
            error_ctx_error_d(errs, extends->span, 2840, "An interface cannot extend a primitive type like '", extends->span,
                "'. It can only extend other named object types.");
        } else if (extends->type.tag != TYPE_IDENTIFIER) {
            error_ctx_error_d(
                errs,
                extends->span,
                2499,
                "An interface can only extend an identifier/qualified-name with optional type arguments."
            );
        }
        // TODO: TS2312 (not an object type)
    }
    for (u32 i = 0; i < my invalid_extends_checked; i++) {
        error_ctx_error_d(errs, expr_outer_span(&my invalid_extends.data[i]), 2499,
            "An interface can only extend an identifier/qualified-name with optional type arguments.");
    }

    interface_typecheck(&my type.interface, my symbol, errs);
    // Once for all declarations of the interface, by the first one that is checked (a lib's only when lib files are
    // checked, `lib: ["dom", "webworker"]` conflicting; nor one in a declaration file under skipLibCheck: a global
    // augmentation in a .d.ts, react-scan's `interface Window`)
    InterfaceDecl const* first_checked = nullptr;
    bool libs_checked = lib_files_checked();
    for (u32 d = 0; my symbol && d < my symbol->decl_count && !first_checked; d++) {
        SymbolDecl const* decl = &my symbol->decls[d];
        SourceFile const* file = decl->kind == SYMBOL_DECL_INTERFACE ? span_source(decl->name) : nullptr;
        // (Nor one whose own symbol is another: an augmentation clone left behind keeps the declarations of the symbol it
        // cloned, whose check is that symbol's. Two augmentations of a re-exported interface through `export *` and
        // through a re-export make two clones, tsc's mergeSymbol; the first's properties are still checked, TS2717,
        // appflare's two tanstack Register augmentations)
        if (file && decl->kind == SYMBOL_DECL_INTERFACE && ((InterfaceDecl const*)decl->node)->symbol != my symbol) continue;
        if (file && (!file->is_stdlib || libs_checked) && source_file_is_checked(file)) first_checked = decl->node;
    }
    if (first_checked == me) {
        // (Every one of them: pothos's global PothosSchemaTypes.SchemaBuilderOptions has 19)
        VecInterfaceMember const** lists = malloc(sizeof(VecInterfaceMember const*) * my symbol->decl_count);
        panic_if(!lists, "out of memory");
        u32 list_count = 0;
        for (u32 d = 0; d < my symbol->decl_count; d++) {
            if (my symbol->decls[d].kind != SYMBOL_DECL_INTERFACE) continue;
            lists[list_count++] = &((InterfaceDecl const*)my symbol->decls[d].node)->type.interface.members;
        }
        // (The declared type first: it makes the later declarations' type parameters the first one's)
        if (list_count > 1) ty_declared_type_of(current_checker, my symbol);
        ty_check_object_type_duplicate_members(current_checker, lists, list_count, errs);
        free(lists);
        if (my symbol->flags & SYMBOL_CLASS) ty_check_class_interface_property_types(current_checker, my symbol, errs);
    }
    if (first_checked == me && !(my symbol->flags & SYMBOL_CLASS)) { // A class checks its own
        ty_check_type_parameter_lists_identical(current_checker, my symbol, errs);
        ty_check_property_modifiers(current_checker, my symbol, nullptr, nullptr, errs);
    }
    // (tsc's checkClassOrInterfaceForDuplicateIndexSignatures runs for the symbol's first declaration only: not at all
    // when that is in a file that isn't checked)
    SymbolDecl const* first_decl = my symbol && my symbol->decl_count ? &my symbol->decls[0] : nullptr;
    if (first_decl && first_decl->node == me) {
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
}
