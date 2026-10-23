#include "ast_walk.h"
#include "source_file.h"
#include "ty/ty_internal.h"
#include "program.h"
#include "compiler_options.h"
#include "scope.h"
#include "enum_decl.h"
#include "class_decl.h"
#include "interface_decl.h"
#include "statement.h"
#include "unused.h"
#include "binding.h"
#include "port_statement.h"
#include <math.h>

// A reference made by lowering a type node (tsc's createTypeReferenceEx with ObjectFlags.FromTypeNode): marked if it's
// made here first, see recursion_identity()
static Ty const* reference_from_type_node(Checker* me, Ty const* target, TyList args) {
    bool outer = my creating_from_type_node;
    my creating_from_type_node = true;
    Ty const* ret = ty_reference(me, target, args);
    my creating_from_type_node = outer;
    return ret;
}

// Lowering: syntax (`TypeNode`) to semantic types (`Ty`), and what symbols are as types (ty_declared_type_of()) and as
// values (ty_type_of_symbol()). Nodes are syntax only: what the parser made.

static Ty const* lower_node(Checker* me, TypeNode const* node, LowerCtx outer_ctx, Symbol const* alias, TyList alias_args);

// tsc's isLocalTypeAlias: a type alias declared inside a function
static bool alias_is_local(Symbol const* symbol) {
    for (u32 i = 0; symbol && i < symbol->decl_count; i++) {
        if (symbol->decls[i].kind == SYMBOL_DECL_TYPE_ALIAS) return ((TypeDecl const*)symbol->decls[i].node)->in_function;
    }
    return false;
}
static inline Ty const* lower(Checker* me, TypeNode const* node, LowerCtx ctx, Symbol const* alias, TyList alias_args);

SymbolLinks* ty_symbol_links(Checker* me, Symbol const* symbol) {
    SymbolLinks* ret = (SymbolLinks*)ty_link_get(me, KEY_SYMBOL_LINKS, symbol);
    if (!ret) {
        ret = (SymbolLinks*)ty_link_publish(me, KEY_SYMBOL_LINKS, symbol, arena_new(&my arena, SymbolLinks));
    }
    return ret;
}

static inline SymbolLinks* symbol_links(Checker* me, Symbol const* symbol) {
    return ty_symbol_links(me, symbol);
}

// The program's version of a symbol. A symbol that comes from inside the AST of the shared lib program (`Array` in
// the declaration of `ReadonlyArray`) is the lib's, which doesn't have what this program has added to it
static inline Symbol const* canonical(Checker* me, Symbol const* symbol) {
    return binder_redirect(my binder, symbol);
}

// A type parameter's declaring item (TyParamState.item), for the ones whose constructor didn't know it: a synthetic one
// made just now, not published yet. A symbol's parameter gets its first declaration from ty_type_param(), which never
// left it null on the suite or the corpora; if one ever does, the first item to get here is it, by a CAS (another
// worker may be setting it from another of the merged declarations)
static void set_param_item(Ty const* param, GenericItem const* item) {
    // (Set already, as always for a symbol's: no write at all to a published parameter, which other workers read
    // plainly)
    if (__atomic_load_n(&param->param.state->item, __ATOMIC_ACQUIRE)) return;
    GenericItem const* expected = nullptr;
    __atomic_compare_exchange_n(&param->param.state->item, &expected, item, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

TyList ty_type_params_of(Checker* me, Generic const* generic) {
    if (generic_empty(generic)) return (TyList){0};
    u64 key[2] = { KEY_GENERIC_PARAMS, (u64)(uintptr_t)generic };
    TyList const* existing = intern_get(&my links, key, 2);
    if (existing) return *existing;

    u32 len = 0;
    Ty const** params = arena_new_n(&my arena, Ty const*, vec_generic_item_size(&generic->items));
    for_each (i, vec_generic_item, &generic->items) {
        Ty const* param = i->symbol_ref ?
            ty_type_param(me, i->symbol_ref) :
            ty_synthetic_type_param(me, span_sv(i->name));
        set_param_item(param, i);
        // (Two of the same name are one symbol, one type parameter: tsc's appendIfUnique in
        // getTypeParametersFromDeclaration)
        bool seen = false;
        for (u32 j = 0; j < len && !seen; j++) seen = params[j] == param;
        if (!seen) params[len++] = param;
    }
    TyList* list = arena_new(&my arena, TyList);
    *list = (TyList){ .data = params, .len = len };
    return *(TyList const*)intern_set(me, &my links, intern_slot(me, &my links, key, 2), list);
}

// The type parameters of a class or interface with several declarations (tsc's
// getLocalTypeParametersOfClassOrInterfaceOrTypeAlias): the distinct names in the order the declarations introduce
// them; `class C<P>` with `interface C<T>` is `C<P, T>` (TS2428). A later declaration's parameter of a name already
// there is the same symbol already (scope_new_generic_merged(), like tsc's binder declaring them in the merged
// symbol's members), except next to a declaration of the shared lib program (`interface Array<T>` in a program): that
// one's own symbol is made to stand for the lib's type parameter here
static TyList merged_local_type_params(Checker* me, Symbol const* symbol, Generic const* first) {
    TyList local = ty_type_params_of(me, first);
    for (u32 d = 0; d < symbol->decl_count; d++) {
        Generic const* other = symbol->decls[d].kind == SYMBOL_DECL_INTERFACE ? &((InterfaceDecl const*)symbol->decls[d].node)->generic :
            symbol->decls[d].kind == SYMBOL_DECL_CLASS ? &((ClassDecl const*)symbol->decls[d].node)->generic : nullptr;
        if (!other || other == first) continue;
        for_each (i, vec_generic_item, &other->items) {
            if (!i->symbol_ref) continue;
            Atom const* name = checker_atom(me, span_sv(i->name));
            Ty const* same = nullptr;
            for (u32 k = 0; k < local.len && !same; k++) if (local.data[k]->param.name == name) same = local.data[k];
            u64 key[2] = { KEY_TYPE_PARAM, (u64)(uintptr_t)i->symbol_ref };
            if (same) {
                // (A no-op for a shared symbol: its slot is `same` already)
                void const** slot = intern_slot(me, &my links, key, 2);
                if (!*slot) intern_set(me, &my links, slot, same);
                continue;
            }
            Ty const* param = ty_type_param(me, i->symbol_ref);
            set_param_item(param, i);
            Ty const** data = arena_new_n(&my arena, Ty const*, local.len + 1);
            if (local.len) memcpy(data, local.data, local.len * sizeof(*data));
            data[local.len] = param;
            local = (TyList){ .data = data, .len = local.len + 1 };
        }
    }
    return local;
}

Span ty_symbol_display_name(Symbol const* symbol) {
    if (span_valid(symbol->name_span) || !(symbol->flags & SYMBOL_CLASS)) return symbol->name_span;
    ClassDecl const* class_decl = symbol_find_node(symbol, SYMBOL_DECL_CLASS);
    return class_decl ? class_decl_display_name(class_decl) : symbol->name_span;
}

// `A.B.name`. TODO(plan 04 stage 3): tsc qualifies a name as far as needed to be reachable from where it's printed
// (symbol accessibility). Always qualifying is what it does for error messages.
static void append_qualified_name(cstr* out, Symbol const* symbol) {
    Symbol const* parent = symbol->parent;
    // Modules and ambient modules (`declare module "m"`) are containers too, but not part of a name
    if (parent && !parent->is_module && !(parent->name.size && (*parent->name.buf == '"' || *parent->name.buf == '\''))) {
        append_qualified_name(out, parent);
        cstr_append_lit(out, ".");
    }
    cstr_append_span(out, ty_symbol_display_name(symbol));
}

Atom const* ty_qualified_name(Checker* me, Symbol const* symbol) {
    Symbol const* parent = symbol->parent;
    if (!parent || parent->is_module) return checker_atom(me, span_sv(ty_symbol_display_name(symbol)));
    cstr qualified = cstr_null;
    append_qualified_name(&qualified, symbol);
    Atom const* ret = checker_atom(me, cstr_sv(&qualified));
    cstr_drop(&qualified);
    return ret;
}

// tsc's isThislessInterface (negated): an interface has a `this` type if one of its declarations mentions `this` or
// extends a class or an interface that has one
static bool interface_has_this(Symbol const* symbol, u32 depth) {
    if (depth > 32) return false;
    for (u32 i = 0; i < symbol->decl_count; i++) {
        if (symbol->decls[i].kind != SYMBOL_DECL_INTERFACE) continue;
        InterfaceDecl const* interface_decl = symbol->decls[i].node;
        if (interface_decl->contains_this) return true;
        for_each (extends, vec_spanned_type, &interface_decl->extends) {
            if (extends->type.tag != TYPE_IDENTIFIER || identifier_type_import_ref(extends->type.identifier)) continue;
            Symbol const* base = extends->type.identifier->target;
            if (!base || !(base->flags & SYMBOL_INTERFACE) || (base->flags & SYMBOL_CLASS)) return true;
            if (base != symbol && interface_has_this(base, depth + 1)) return true;
        }
    }
    return false;
}

// The type parameters of the scopes a declaration is in (tsc's getOuterTypeParameters), outermost first
TyList ty_outer_type_params(Checker* me, OuterGenerics const* outer) {
    if (!outer) return (TyList){0};
    TyList parent = ty_outer_type_params(me, outer->parent);
    TyList own = ty_type_params_of(me, outer->generic);
    if (!parent.len) return own;
    Ty const** data = arena_new_n(&my arena, Ty const*, parent.len + own.len);
    memcpy(data, parent.data, parent.len * sizeof(*data));
    memcpy(data + parent.len, own.data, own.len * sizeof(*data));
    return (TyList){ .data = data, .len = parent.len + own.len };
}

static Ty const* make_declared(Checker* me, Symbol const* symbol, TyDeclaredKind kind) {
    Ty* ret = ty_alloc(me, TY_DECLARED, TYF_OBJECT);
    ret->declared.kind = kind;
    ret->declared.symbol = symbol;
    ret->declared.name = ty_qualified_name(me, symbol);
    ret->declared.state = arena_new(&my arena, TyObjectState);
    if (kind == TY_DECLARED_INTERFACE || kind == TY_DECLARED_CLASS_INSTANCE || kind == TY_DECLARED_CLASS_STATIC) {
        ret->declared.outer = symbol_outer_generics(symbol);
        TyList outer = ty_outer_type_params(me, ret->declared.outer);
        ret->declared.outer_count = outer.len;
        ret->declared.type_params = outer;
    }
    if (kind == TY_DECLARED_INTERFACE || kind == TY_DECLARED_CLASS_INSTANCE) {
        Generic const* first = symbol_type_generic(symbol);
        TyList local = merged_local_type_params(me, symbol, first);
        TyList outer = ret->declared.type_params;
        if (!outer.len) {
            ret->declared.type_params = local;
        } else if (local.len) {
            Ty const** data = arena_new_n(&my arena, Ty const*, outer.len + local.len);
            memcpy(data, outer.data, outer.len * sizeof(*data));
            memcpy(data + outer.len, local.data, local.len * sizeof(*data));
            ret->declared.type_params = (TyList){ .data = data, .len = outer.len + local.len };
        }
        // (A class merged with an interface is a class: tsc's getDeclaredTypeOfClassOrInterface)
        if (kind == TY_DECLARED_CLASS_INSTANCE || (symbol->flags & SYMBOL_CLASS) || interface_has_this(symbol, 0)) {
            ret->declared.this_param = ty_this_type_param(me, ret);
        }
        ret->declared.raw = arena_new(&my arena, TyObjectState);
    }
    return ret;
}

#define SYMBOL_DECLARED_CIRCULAR ((uintptr_t)1)
static_assert(_Alignof(Ty) >= 2, "SymbolLinks.declared tags a Ty's low bit");
#define DECLARED_TYPE(v) ((Ty const*)((v) & ~SYMBOL_DECLARED_CIRCULAR))

// An enum member's declared type, finished by its enum's resolution (which started at `taint`: what it made so far is
// clean if that is, whatever comes after), whether the member's own computation has it in flight here or not
void ty_set_declared_type(Checker* me, Symbol const* symbol, Ty const* type, u64 taint) {
    SymbolLinks* links = symbol_links(me, symbol);
    sw_finish_fast(me, &links->declared, &links->declared_state, (uintptr_t)type, taint);
}

// An enum member's type is made when its enum's is
static Ty const* enum_member_type(Checker* me, Symbol const* symbol) {
    if (!symbol->parent || !(symbol->parent->flags & SYMBOL_ENUM)) return ty_intrinsic(me, TY_ERROR);
    ty_declared_type_of(me, symbol->parent);
    SymbolLinks* links = symbol_links(me, symbol);
    uintptr_t v = sw_peek_fast(me, &links->declared);
    // (Null: the enum is being resolved further up the stack and hasn't got to this member yet)
    return v ? DECLARED_TYPE(v) : nullptr;
}

// `type X = intrinsic` (the lib's Uppercase<S> & co.): the builtin name, not a type called `intrinsic`
static bool is_intrinsic_body(TypeNode const* body) {
    return body->tag == TYPE_IDENTIFIER && body->identifier->intrinsic_keyword;
}

// A block's local type alias after an exported one of its name (`export type A = {}; type A = {}`, no error): tsc
// declares the export in the block's locals too (declareModuleMember's local symbol, no meaning of its own), so the
// local declaration merges into that local symbol, whose first type alias declaration is the export's. That export,
// else null
static Symbol const* exported_alias_twin(Symbol const* symbol, SymbolDecl const* decl) {
    if (!symbol->is_local || decl->kind != SYMBOL_DECL_TYPE_ALIAS || !decl->block) return nullptr;
    Symbol const* container = decl->block->container;
    Symbol const* twin = container && container->exports ? symbol_table_get(container->exports, symbol->name) : nullptr;
    if (!twin || twin == symbol) return nullptr;
    for (u32 i = 0; i < twin->decl_count; i++) {
        SymbolDecl const* other = &twin->decls[i];
        if (other->kind == SYMBOL_DECL_TYPE_ALIAS && (other->flags & SYMBOL_TYPE_ALIAS) && other->block == decl->block &&
            span_source(other->full) == span_source(decl->full) && span_start_ptr(other->full) < span_start_ptr(decl->full)
        ) {
            return twin;
        }
    }
    return nullptr;
}

// A type alias asked for while in flight that isn't being resolved above the innermost resolution barrier, a call's
// first resolution or a variance measurement's (ty_resolving_reenters()): tsc's pushTypeResolution finds no cycle and
// resolves it again. (Bounded: the nested resolution is above the barrier.) opencode's `type P = Schema.Type<typeof
// P>` beside `const P = withStatics(..., { make: (input: Omit<P, "id">): P => ... })`, reached from the alias first:
// the call's inference meets the alias again below its barrier
static bool alias_reenters(Checker* me, Symbol const* symbol) {
    SymbolFlags other = SYMBOL_INTERFACE | SYMBOL_CLASS | SYMBOL_ENUM | SYMBOL_ENUM_MEMBER | SYMBOL_TYPE_PARAMETER;
    if (!(symbol->flags & SYMBOL_TYPE_ALIAS) || (symbol->flags & other)) return false;
    return ty_resolving_reenters(me, RESOLVING_ALIAS, symbol);
}

// The type a symbol stands for in a type position: the symbol must be the one that has the type meaning (what
// identifiers keep as `target`). Everything about a placeholder is any
Ty const* ty_declared_type_of(Checker* me, Symbol const* symbol) {
    symbol = canonical(me, symbol);
    if (symbol->is_placeholder) return ty_intrinsic(me, TY_ERROR);
    SymbolLinks* links = symbol_links(me, symbol);
    // (A shared word, plan 86 stage 4f: published when clean, this worker's own otherwise)
    uintptr_t v = sw_get(me, &links->declared);
    if (__builtin_expect(v != 0, 1)) return DECLARED_TYPE(v);
    switch (sw_look_fast(me, &links->declared, &links->declared_state, &v)) {
        case SW_LOOK_DONE: return DECLARED_TYPE(v);
        case SW_LOOK_MINE:
            // An alias in flight below the resolution barrier (a variance measurement's, a call's) is resolved again,
            // nested, and the first to finish is its type (tsc's findResolutionCycleStartIndex looks no further down)
            if (alias_reenters(me, symbol)) {
                if (debug_env.debug_reentry) ty_debug_reentry(me, "REENTER 2 alias", symbol->decls[0].name);
                break;
            }
            // tsc's pushTypeResolution: every alias from this one on is part of the cycle, and fails when it finishes
            ty_resolving_cycle(me, RESOLVING_ALIAS, symbol);
            return ty_intrinsic(me, TY_ERROR);
        case SW_LOOK_COMPUTE: break;
    }
    u64 taint = sw_claim_fast(me, &links->declared, &links->declared_state);
    Ty const* ret;
    bool alias_circular = false;
    // An interface's, a class's and a type parameter's declared type is an identity made of identities (the symbol's
    // type parameters): published whatever happened meanwhile, or each worker would have its own interface (a loser's
    // fresh one is referenced by nothing but its own this-type parameter)
    bool identity = (symbol->flags & (SYMBOL_INTERFACE | SYMBOL_CLASS)) ||
        (!(symbol->flags & (SYMBOL_ENUM | SYMBOL_ENUM_MEMBER)) && (symbol->flags & SYMBOL_TYPE_PARAMETER));
    if (symbol->flags & SYMBOL_INTERFACE) {
        ret = make_declared(me, symbol, TY_DECLARED_INTERFACE);
    } else if (symbol->flags & SYMBOL_CLASS) {
        ret = make_declared(me, symbol, TY_DECLARED_CLASS_INSTANCE);
    } else if (symbol->flags & SYMBOL_ENUM) {
        ret = ty_resolve_enum(me, symbol, taint);
    } else if (symbol->flags & SYMBOL_ENUM_MEMBER) {
        ret = enum_member_type(me, symbol);
        if (!ret) {
            // (Its enum is in flight above: not computed, a cycle like any, and the taint event was counted)
            sw_abandon(me, &links->declared, &links->declared_state);
            return ty_intrinsic(me, TY_ERROR);
        }
    } else if (symbol->flags & SYMBOL_TYPE_PARAMETER) {
        ret = ty_type_param(me, symbol);
    } else if (symbol->flags & SYMBOL_TYPE_ALIAS) {
        SymbolDecl const* decl = &symbol->decls[0];
        for (u32 i = symbol->decl_count; i--;) {
            if (symbol->decls[i].flags & SYMBOL_TYPE_ALIAS) decl = &symbol->decls[i];
        }
        Symbol const* twin = exported_alias_twin(symbol, decl);
        if (twin) {
            // tsc's local symbol has the export's declaration first: its type is the export's (the type of that
            // declaration's node, whose alias symbol is the export)
            ret = ty_declared_type_of(me, twin);
        } else if (decl->kind == SYMBOL_DECL_TYPE_ALIAS) {
            TypeDecl const* type_decl = decl->node;
            TyList params = ty_type_params_of(me, &type_decl->generic);
            // (A local alias can mention the type parameters around it too, through `typeof` of a contextually typed
            // parameter as well: `t => { type A = (typeof t)[] extends ... }` with t a contextual signature's T)
            LowerCtx ctx = { .generic = params.len > 0 || type_decl->outer_generics || type_decl->local, .in_alias = true };
            TypeNode const* body = &type_decl->type;
            if (is_intrinsic_body(body)) {
                // `type X = intrinsic`: what the checker makes of the name. The string mappings and NoInfer are made
                // where they're referenced (lower_identifier()), this is what the bare name is
                bool undefined = csview_equals(symbol->name, "BuiltinIteratorReturn") &&
                    compiler_options()->strict_builtin_iterator_return;
                ret = ty_intrinsic(me, undefined ? TY_UNDEFINED : TY_ANY);
            } else {
                ty_resolving_push(me, RESOLVING_ALIAS, symbol);
                ret = lower(me, body, ctx, symbol, params);
                alias_circular = ty_resolving_pop(me);
                if (alias_circular) {
                    ty_error_at(me, decl->name, 2456, cstr_d("Type alias '", symbol->name, "' circularly references itself."));
                    ret = ty_intrinsic(me, TY_ERROR);
                }
            }
        } else if (decl->kind == SYMBOL_DECL_BUILTIN) {
            // The `object` keyword is a builtin alias whose node is an empty interface
            ret = csview_equals(symbol->name, "object") ?
                ty_intrinsic(me, TY_NON_PRIMITIVE) :
                ty_from_node(me, decl->node, (LowerCtx){0});
        } else {
            ret = ty_intrinsic(me, TY_ERROR);
        }
    } else {
        ret = ty_intrinsic(me, TY_ERROR); // A value used as a type, TS2749
    }
    // symbol_links() pointers are stable (arena), the table growing in the meantime doesn't matter. (A nested
    // resolution of an alias that re-entered may have finished first: its type and circularity stay, tsc's `if
    // links.declaredType == nil`; tsc's typeParameters aren't unset by a later failure either, and this one's TS2456 is
    // said above)
    if (identity) taint = graph_taint_now;
    if (sw_mine_in_flight_fast(me, &links->declared, &links->declared_state)) {
        SW_FP(TK_SYMBOL_DECLARED, symbol, ret, taint);
        v = sw_finish_fast(me, &links->declared, &links->declared_state, (uintptr_t)ret | (alias_circular ? SYMBOL_DECLARED_CIRCULAR : 0), taint);
    } else {
        // (Finished by a nested resolution, or an enum member by its enum's)
        sw_unclaim(me, &links->declared);
        v = sw_peek_fast(me, &links->declared);
        check(v);
    }
    return DECLARED_TYPE(v);
}

// Whether a type alias's resolution reached itself (TS2456): tsc's getDeclaredTypeOfTypeAlias then never sets
// typeAliasLinks.typeParameters, so every reference takes it as not generic (getTypeFromTypeAliasReference)
bool ty_alias_is_circular(Checker* me, Symbol const* symbol) {
    if (!(symbol->flags & SYMBOL_TYPE_ALIAS) || symbol->is_placeholder) return false;
    // (Asked for every alias reference: one lookup when the alias is resolved, as it nearly always is by then. Links are
    // arena-allocated, the pointer stays valid)
    SymbolLinks* links = symbol_links(me, canonical(me, symbol));
    uintptr_t v = sw_get(me, &links->declared);
    if (!v) {
        ty_declared_type_of(me, symbol);
        v = sw_peek_fast(me, &links->declared);
    }
    return (v & SYMBOL_DECLARED_CIRCULAR) != 0;
}

// The static side of a class, the object of an enum: what the symbol is as a value, before any merging
Ty const* ty_static_type_of(Checker* me, Symbol const* symbol) {
    symbol = canonical(me, symbol);
    SymbolLinks* links = symbol_links(me, symbol);
    Ty const* known = __atomic_load_n(&links->static_type, __ATOMIC_ACQUIRE);
    if (!known) {
        // (An identity: made whole first, then published, the first checker's winning with --shared-graph)
        Ty const* made = make_declared(me, symbol, symbol->flags & SYMBOL_CLASS ? TY_DECLARED_CLASS_STATIC : TY_DECLARED_ENUM_OBJECT);
        known = nullptr;
        if (__atomic_compare_exchange_n(&links->static_type, &known, made, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) known = made;
    }
    return known;
}

// Whether a class or interface reference is in a JavaScript file (tsc's getTypeFromClassOrInterfaceReference passes
// isInJSFile(node) to fillMissingTypeArguments; an alias's instantiation passes isInJSFile of the alias's value
// declaration, which it has none of, so not for those)
static bool identifier_in_js(IdentifierType const* identifier) {
    SourceFile const* file = span_source(identifier_type_span(identifier));
    return file && file->is_js;
}

// Type arguments as written, padded with defaults (then `any`; `js`: see identifier_in_js()) and truncated to the
// parameter count. The first `outer` parameters are a local class or interface's outer ones, passed as themselves
static TyList lower_type_args(
    Checker* me, TyList params, u32 outer, VecSpannedType const* written, LowerCtx ctx, Ty const* args[MAX_TYPE_ARGS], bool js
) {
    panic_if(params.len > MAX_TYPE_ARGS, "too many type parameters");
    u32 written_len = (u32)vec_spanned_type_size(written) + outer;
    // (Those not filled in yet are the error type while a default is instantiated: tsc's fillMissingTypeArguments)
    for (u32 i = 0; i < params.len; i++) args[i] = ty_intrinsic(me, TY_ERROR);
    for (u32 i = 0; i < params.len; i++) {
        if (i < outer) {
            args[i] = params.data[i];
        } else if (i < written_len) {
            args[i] = ty_from_node(me, &vec_spanned_type_at(written, i - outer)->type, ctx);
        } else {
            Ty const* fallback = ty_param_default(me, params.data[i]);
            // In JavaScript a default of `{}` or unknown is any (tsc's fillMissingTypeArguments with isInJSFile)
            if (fallback && js && ((fallback->flags & TYF_UNKNOWN) ||
                ty_is_related(me, fallback, ty_empty_object_type(me), TY_RELATION_IDENTITY))
            ) {
                fallback = ty_intrinsic(me, TY_ANY);
            }
            if (fallback) {
                // A default can mention the parameters before it
                // Arena-allocated: instantiation can keep the mapper (deferred conditional and object types)
                TyMapper const* mapper = ty_mapper_new(me, params, (TyList){ .data = args, .len = params.len });
                fallback = ty_instantiate(me, fallback, mapper);
            }
            // TODO: TS2314 when there's no default. tsc fills in the error type too
            args[i] = fallback ? fallback : ty_intrinsic(me, written_len > outer ? TY_ERROR : TY_ANY);
        }
    }
    return (TyList){ .data = args, .len = params.len };
}

// Whether alias body `declared`, a distributive conditional type, distributes over its check type parameter's
// argument: a union once reduced (tsc's getConditionalTypeInstantiation, distributionType)
static bool distributes_over_union(Checker* me, Ty const* declared, TyList params, TyList args) {
    if (declared->kind != TY_CONDITIONAL || !declared->conditional.distributive) return false;
    for (u32 i = 0; i < params.len && i < args.len; i++) {
        if (params.data[i] != declared->conditional.check) continue;
        Ty const* arg = args.data[i];
        return arg->kind == TY_UNION && ty_reduced(me, arg)->kind == TY_UNION;
    }
    return false;
}

static Ty const* alias_instance(Checker* me, Symbol const* decl, Ty const* body, TyList params, TyList args) {
    u64 stack_key[32];
    u32 count = 2 + args.len;
    check(count <= c_arraylen(stack_key));
    stack_key[0] = KEY_ALIAS_INSTANCE;
    stack_key[1] = (u64)(uintptr_t)decl;
    for (u32 i = 0; i < args.len; i++) stack_key[2 + i] = args.data[i]->id;
    Ty const* ret = intern_get(&my links, stack_key, count);
    if (!ret) {
        TyMapper const* mapper = ty_mapper_new(me, params, args);
        ret = ty_instantiate(me, body, mapper);
        // (Its type parameters unused, the body is itself: it still goes by these arguments)
        if (ret == body && body->alias == decl) ret = ty_realias(me, ret, decl, args);
        *intern_slot(me, &my links, stack_key, count) = ret;
    }
    return ret;
}

static bool symbol_is_generic_alias(Symbol const* symbol) {
    return symbol->flags & SYMBOL_TYPE_ALIAS && !symbol->is_placeholder && symbol_type_generic(symbol);
}

// A type's alias type arguments, the defaults filled in where it goes by a generic alias with fewer (an import's
// reference with the type arguments written, see lower_identifier(): tsc's fillMissingTypeArguments where it compares them)
TyList ty_alias_args_filled(Checker* me, Ty const* ty, Ty const* buf[MAX_TYPE_ARGS]) {
    if (!ty->alias || !symbol_is_generic_alias(ty->alias)) return ty->alias_args;
    TyList params = ty_alias_type_params(me, ty->alias);
    if (ty->alias_args.len >= params.len || params.len > MAX_TYPE_ARGS) return ty->alias_args;
    VecSpannedType none = {0};
    if (!ty->alias_args.len) return lower_type_args(me, params, 0, &none, (LowerCtx){0}, buf, false);
    // (Those written, then the defaults: tsc's fillMissingTypeArguments, a default mentioning the parameters before it)
    for (u32 i = 0; i < params.len; i++) buf[i] = i < ty->alias_args.len ? ty->alias_args.data[i] : ty_intrinsic(me, TY_ERROR);
    for (u32 i = ty->alias_args.len; i < params.len; i++) {
        Ty const* fallback = ty_param_default(me, params.data[i]);
        if (fallback) fallback = ty_instantiate(me, fallback, ty_mapper_new(me, params, (TyList){ .data = buf, .len = params.len }));
        buf[i] = fallback ? fallback : ty_intrinsic(me, TY_ERROR);
    }
    return (TyList){ .data = buf, .len = params.len };
}

// The instantiation of a generic type alias (canonical) with these type arguments, as `A<args>` written would be
Ty const* ty_alias_instance(Checker* me, Symbol const* decl, TyList args) {
    Ty const* declared = ty_declared_type_of(me, decl);
    return alias_instance(me, decl, declared, ty_alias_type_params(me, decl), args);
}

TyList ty_alias_type_params(Checker* me, Symbol const* decl) {
    return ty_type_params_of(me, symbol_type_generic(decl));
}

// tsc's getGlobalTypeAliasSymbol without its errors: the global type alias `name` when it has `arity` type parameters
// (a file's own `type NonNullable<T, U>` with noLib is no NonNullable for the checker to instantiate)
Symbol const* ty_global_type_alias(Checker* me, csview name, u32 arity) {
    Symbol const* symbol = program_get_global(my program, name, SYMBOL_TYPE);
    if (symbol) symbol = binder_redirect(my binder, symbol);
    if (!symbol || !(symbol->flags & SYMBOL_TYPE_ALIAS) || ty_alias_type_params(me, symbol).len != arity) return nullptr;
    return symbol;
}

// The same with tsc's reportErrors: TS2318 (once, a global diagnostic) when there is no global type alias of the name
// (tsc's getGlobalSymbol with SymbolFlags.TypeAlias: a global `interface Omit<T, K>` is none). The arity's TS2317 isn't
Symbol const* ty_global_type_alias_reported(Checker* me, csview name, u32 arity) {
    Symbol const* symbol = ty_global_type_alias(me, name, arity);
    Symbol const* named = program_get_global(my program, name, SYMBOL_TYPE);
    if (named) named = binder_redirect(my binder, named);
    if (!symbol && !(named && (named->flags & SYMBOL_TYPE_ALIAS))) ty_report_missing_global_type(me, name);
    return symbol;
}

void ty_report_missing_global_type(Checker* me, csview name) {
    error_ctx_global_error_d(my errs, 2318, "Cannot find global type '", name, "'.");
}

static Ty const* lower_identifier(Checker* me, IdentifierType const* identifier, LowerCtx ctx, Symbol const* body_of);

// An instantiation of a generic alias whose body is a union or intersection, made of the same constituents
static bool unchanged_composite_body(Ty const* declared, Ty const* ret, Symbol const* decl) {
    if (declared->alias != decl || ret == declared || ret->kind != declared->kind) return false;
    if (declared->kind != TY_UNION && declared->kind != TY_INTERSECTION) return false;
    if (declared->flags & TYF_BOOLEAN) return false;
    if (ret->composite.types.len != declared->composite.types.len) return false;
    for (u32 i = 0; i < ret->composite.types.len; i++) {
        if (ret->composite.types.data[i] != declared->composite.types.data[i]) return false;
    }
    return true;
}

// `{ [P in keyof T]: X }` with T a type parameter: instantiating it keeps the name it was written with (tsc's
// instantiateMappedType maps the constituents without the new alias), only a union made of it takes the new one
static bool is_homomorphic_mapped_node(TypeNode const* node) {
    if (node->tag != TYPE_INTERFACE || vec_interface_member_size(&node->interface.members) != 1) return false;
    InterfaceMember const* member = vec_interface_member_at(&node->interface.members, 0);
    if (member->key.tag != INTERFACE_KEY_MAPPED || member->key.mapped->in_type.tag != TYPE_KEYOF) return false;
    TypeNode const* operand = &member->key.mapped->in_type.keyof->interface;
    return operand->tag == TYPE_IDENTIFIER && operand->identifier->target &&
        (operand->identifier->target->flags & SYMBOL_TYPE_PARAMETER);
}

// The body of a type alias as written (the last declaration, like ty_declared_type_of()). Null if there's none
static TypeNode const* alias_body_node(Symbol const* decl) {
    TypeNode const* ret = nullptr;
    for (u32 i = 0; decl && i < decl->decl_count; i++) {
        if (decl->decls[i].kind == SYMBOL_DECL_TYPE_ALIAS) ret = &((TypeDecl const*)decl->decls[i].node)->type;
    }
    return ret;
}

// A reference to one of the lib's intrinsic generic aliases: `Uppercase<T>` & co. are string mappings (tsc's
// getStringMappingType). Null if the alias isn't one
static Ty const* intrinsic_alias_instance(Checker* me, Symbol const* decl, Ty const* arg) {
    static char const* const mappings[] = { "Uppercase", "Lowercase", "Capitalize", "Uncapitalize" };
    TypeNode const* body = alias_body_node(decl);
    if (!body || !is_intrinsic_body(body)) return nullptr;
    for (u32 i = 0; i < c_arraylen(mappings); i++) {
        if (csview_equals(decl->name, mappings[i])) return ty_string_mapping(me, (TyStringMapping)i, decl, arg);
    }
    if (csview_equals(decl->name, "NoInfer")) return ty_no_infer(me, arg);
    return nullptr;
}



// The type of a type reference whose name doesn't resolve: tsc's unresolvedType, which prints as written
static Ty const* unresolved_reference(Checker* me, IdentifierType const* identifier, LowerCtx ctx) {
    // (A missing name, `a.` at the end of a line: no node to reuse, errorType)
    if (identifier->keyword || !span_valid(identifier->name) || !identifier->name.size) return ty_intrinsic(me, TY_ERROR);
    cstr name = cstr_null;
    for_each (i, ast_spans, identifier_type_namespaces(identifier)) {
        cstr_append_sv(&name, span_sv(*i));
        cstr_append(&name, ".");
    }
    cstr_append_sv(&name, span_sv(identifier->name));
    // (The type arguments' types: tsc's typeArgumentsFromTypeReferenceNode, printed like any type, `KK` of a literal as
    // the literal)
    u32 arg_count = (u32)vec_spanned_type_size(&identifier->generic_params);
    Ty const* stack_args[8];
    Ty const** args = arg_count <= c_arraylen(stack_args) ? stack_args : malloc(sizeof(Ty const*) * arg_count);
    panic_if(!args, "out of memory");
    for (u32 i = 0; i < arg_count; i++) args[i] = ty_from_node(me, &vec_spanned_type_at(&identifier->generic_params, i)->type, ctx);
    Ty const* ret = ty_unresolved(me, cstr_sv(&name), (TyList){ .data = args, .len = arg_count });
    if (args != stack_args) free(args);
    cstr_drop(&name);
    return ret;
}

// `import { X } from "m"` of a property of what m's `export =` names, used as a type: tsc's getExternalModuleMember
// takes the property's own symbol, which is a type too when it's a namespace export the value's type has as a
// property (a class merged with a namespace, or a base class of one: node's `stream` and its `EventEmitter`). The
// binder can't know, so the type side is decided here (resolve_alias_target(), identifier_type_lookup_names()).
// Null for none: missing (TS2305 comes from the import) or a value only (TS2749 here)
static Symbol const* export_equals_property_resolve(
    Checker* me, Symbol const* property, IdentifierType const* identifier, bool report, bool* missing, bool* global_type
) {
    *missing = false;
    if (global_type) *global_type = false;
    Ty const* assigned = ty_type_of_symbol(me, property->decls[0].node);
    if (assigned->flags & TYF_ERROR) return nullptr;
    Ty const* holder = assigned->flags & (TYF_OBJECT | TYF_INTERSECTION) ? assigned : ty_apparent_type(me, assigned);
    Prop const* prop = holder->kind == TY_UNION || holder->kind == TY_INTERSECTION ? nullptr :
        ty_get_prop(me, holder, checker_atom(me, property->name));
    while (prop && prop->target) prop = prop->target;
    Symbol* symbol = prop ? (Symbol*)prop->decl->symbol : nullptr;
    if (symbol && symbol_resolved_flags(symbol) & SYMBOL_TYPE) {
        Symbol const* type = symbol_for_meaning(symbol, SYMBOL_TYPE);
        if (type && !type->is_placeholder) {
            // A use of the import as a type (noUnusedLocals): the binder can't know, a value-only property used as a
            // type (TS2749, webpack's JSDoc `@returns {TRANSITIVE}`) doesn't count, tsc's resolveName failed for it
            if (report && identifier->symbol && unused_enabled()) {
                __atomic_fetch_or(&((Symbol*)identifier->symbol)->referenced, SYMBOL_TYPE, __ATOMIC_RELAXED);
            }
            return canonical(me, type);
        }
    }
    if (!prop && ty_export_equals_property_missing(me, property)) {
        // (tsc's resolveName found the import, whose target is unknownSymbol: a use)
        if (report && identifier->symbol && unused_enabled()) {
            __atomic_fetch_or(&((Symbol*)identifier->symbol)->referenced, SYMBOL_TYPE, __ATOMIC_RELAXED);
        }
        *missing = true;
        return nullptr;
    }
    // A value-only property isn't a type: tsc's resolveName goes on past the import, and imports live in module
    // scope, whose parent is the global one (lib.dom's `Plugin` for a `Plugin` value property)
    Symbol const* global = program_get_global(me->program, span_sv(identifier->name), SYMBOL_TYPE);
    if (global) {
        if (global_type) *global_type = true;
        return canonical(me, global);
    }
    if (report) {
        ty_error_at(me, identifier->name, 2749, cstr_d("'", span_sv(identifier->name),
            "' refers to a value, but is being used as a type here. Did you mean 'typeof ", span_sv(identifier->name), "'?"));
    }
    return nullptr;
}

static Symbol const* export_equals_property_type(Checker* me, Symbol const* property, IdentifierType const* identifier) {
    bool missing;
    return export_equals_property_resolve(me, property, identifier, true, &missing, nullptr);
}

Symbol const* ty_export_equals_property_shown(Checker* me, IdentifierType const* identifier) {
    Symbol const* decl = identifier->target ? canonical(me, identifier->target) : nullptr;
    if (!decl || !decl->decl_count || decl->decls[0].kind != SYMBOL_DECL_EXPORT_EQUALS_PROPERTY ||
        identifier_type_namespace_count(identifier) || span_valid(identifier_type_import_span(identifier))
    ) {
        return nullptr;
    }
    bool missing;
    Symbol const* ret = export_equals_property_resolve(me, decl, identifier, false, &missing, nullptr);
    return ret ? ret : missing ? identifier->symbol : nullptr;
}

bool ty_export_equals_property_means_global(Checker* me, IdentifierType const* identifier) {
    Symbol const* decl = identifier->target ? canonical(me, identifier->target) : nullptr;
    if (!decl || !decl->decl_count || decl->decls[0].kind != SYMBOL_DECL_EXPORT_EQUALS_PROPERTY ||
        identifier_type_namespace_count(identifier) || span_valid(identifier_type_import_span(identifier))
    ) {
        return false;
    }
    bool missing, global;
    export_equals_property_resolve(me, decl, identifier, false, &missing, &global);
    return global;
}

// tsc checks every type reference (checkTypeReferenceNode): one to a property of what `export =` names is TS2749 when
// that property is a value only, also in an annotation nothing reads (an unused interface member's type)
void ty_check_export_equals_property_reference(Checker* me, IdentifierType const* identifier) {
    Symbol const* decl = identifier->target ? canonical(me, identifier->target) : nullptr;
    if (decl && decl->decl_count && decl->decls[0].kind == SYMBOL_DECL_EXPORT_EQUALS_PROPERTY &&
        !identifier_type_namespace_count(identifier) && !span_valid(identifier_type_import_span(identifier))
    ) {
        export_equals_property_type(me, decl, identifier);
    }
}

// `body_of`: the alias whose body the reference is, if it is one
static Ty const* lower_identifier(Checker* me, IdentifierType const* identifier, LowerCtx ctx, Symbol const* body_of) {
    switch ((JsDocIntrinsic)identifier->jsdoc_intrinsic) {
        case JSDOC_INTRINSIC_NONE: break;
        case JSDOC_INTRINSIC_STRING: return ty_intrinsic(me, TY_STRING);
        case JSDOC_INTRINSIC_NUMBER: return ty_intrinsic(me, TY_NUMBER);
        case JSDOC_INTRINSIC_BIGINT: return ty_intrinsic(me, TY_BIGINT);
        case JSDOC_INTRINSIC_BOOLEAN: return ty_intrinsic(me, TY_BOOLEAN);
        case JSDOC_INTRINSIC_VOID: return ty_intrinsic(me, TY_VOID);
        case JSDOC_INTRINSIC_UNDEFINED: return ty_intrinsic(me, TY_UNDEFINED);
        case JSDOC_INTRINSIC_NULL: return ty_intrinsic(me, TY_NULL);
        case JSDOC_INTRINSIC_ANY: return ty_intrinsic(me, TY_ANY);
        case JSDOC_INTRINSIC_INDEX_SIGNATURE: {
            // (tsgo's getIntendedTypeFromJSDocTypeReference: the global Record alias of a valid key, else any)
            Symbol const* record = ty_global_type_alias(me, c_sv("Record"), 2);
            Ty const* key = ty_from_node(me, &vec_spanned_type_at(&identifier->generic_params, 0)->type, ctx);
            if (!record || !ty_is_valid_index_key(key)) return ty_intrinsic(me, TY_ANY);
            Ty const* args[2] = { key, ty_from_node(me, &vec_spanned_type_at(&identifier->generic_params, 1)->type, ctx) };
            return ty_alias_instance(me, record, (TyList){ .data = args, .len = 2 });
        }
    }
    Symbol const* decl = canonical(me, identifier->target);
    // An import of an interface that the value of the module's `export =` has as a property too: tsc's combined symbol,
    // whose declared type is an interface of its own (SymbolAlias.combined_type)
    Symbol const* combined = decl && !identifier_type_namespace_count(identifier) ? ty_alias_combined_type(me, identifier->symbol) : nullptr;
    if (combined) decl = combined;
    if (decl && decl->decl_count && decl->decls[0].kind == SYMBOL_DECL_EXPORT_EQUALS_PROPERTY &&
        !identifier_type_namespace_count(identifier) && !span_valid(identifier_type_import_span(identifier))
    ) {
        decl = export_equals_property_type(me, decl, identifier);
        // (Missing, or a value used as a type: errorType, printed by the reference as written)
        if (!decl) return unresolved_reference(me, identifier, ctx);
    }
    if (identifier->rare && identifier->rare->import_export_equals_alias && decl && !decl->is_placeholder) {
        // (tsc's getTypeReferenceType of the `export=` alias: tryGetDeclaredTypeOfSymbol, checkNoTypeArguments)
        if (vec_spanned_type_size(&identifier->generic_params)) {
            ty_error_at(me, identifier_type_import_span(identifier), 2315, cstr_d("Type 'export=' is not generic."));
            return ty_intrinsic(me, TY_ERROR);
        }
        return ty_declared_type_of(me, decl);
    }
    if (span_valid(identifier_type_import_span(identifier))) {
        // `import("./a").B`: an unresolved one was reported and is the error type (tsc's getTypeFromImportTypeNode), one
        // of a shorthand ambient module is any by design
        if (!decl || decl->is_placeholder) return ty_intrinsic(me, !decl || decl->is_unresolved ? TY_ERROR : TY_ANY);
        if (identifier_type_import_typeof(identifier) || identifier->rare->import_prop_start >= 0) {
            Ty const* ret = ty_type_of_symbol(me, decl);
            isize count = identifier_type_namespace_count(identifier) + 1;
            Prop const* previous = nullptr;
            for (isize i = identifier->rare->import_prop_start; i >= 0 && i < count; i++) {
                Span name = i == count - 1 ? identifier->name : *ast_spans_at(identifier_type_namespaces(identifier), i);
                // (tsc's getPropertyOfType: an intersection's or a union's too, vitest's `typeof import("vitest").it.runIf`)
                Prop const* prop = ty_property_of_type(me, ret, checker_atom(me, span_sv(name)));
                if (!prop) {
                    // A property of what `module.exports =` assigns or of a value, like the names before it (tsc's
                    // getTypeFromImportTypeNode reports it as a missing export of what the previous name resolved
                    // to: getFullyQualifiedName of that symbol, a property's with its parent's)
                    cstr container_name = cstr_null;
                    if (!previous) {
                        // (The export as found, an `export { it }` specifier's alias in the module: tsc's currentNamespace)
                        isize at = identifier->rare->import_prop_start - 1;
                        Symbol const* found = at >= 0 && at < ast_symbols_size(identifier_type_namespace_symbols(identifier)) ?
                            *ast_symbols_at(identifier_type_namespace_symbols(identifier), at) : nullptr;
                        container_name = symbol_fully_qualified_name(found ? found : decl);
                    } else if (previous->parent) {
                        container_name = symbol_fully_qualified_name(previous->parent);
                        cstr_append_d(&container_name, DEFAULT_DISPLAY, ".", atom_sv(previous->name));
                    } else {
                        // (A property of an anonymous type: tsc's symbolToString names it by the declarations it is
                        // reached through, `obj.a`)
                        container_name = cstr_from_sv(decl->name);
                        for (isize j = identifier->rare->import_prop_start; j < i; j++) {
                            cstr_append_d(&container_name, DEFAULT_DISPLAY, ".", span_sv(*ast_spans_at(identifier_type_namespaces(identifier), j)));
                        }
                    }
                    ty_error_at(me, name, 2694, cstr_d("Namespace '", container_name, "' has no exported member '", span_sv(name), "'."));
                    cstr_drop(&container_name);
                    return ty_intrinsic(me, TY_ERROR);
                }
                previous = prop;
                ret = prop_type(me, prop);
            }
            // `typeof import("./m").f<A>`: an instantiation expression (tsc's resolveImportSymbolType,
            // getInstantiationExpressionType)
            u32 arg_count = (u32)vec_spanned_type_size(&identifier->generic_params);
            if (identifier_type_import_typeof(identifier) && arg_count) {
                Ty const** args = malloc(sizeof(Ty const*) * arg_count);
                panic_if(!args, "out of memory");
                for (u32 i = 0; i < arg_count; i++) args[i] = ty_from_node(me, &vec_spanned_type_at(&identifier->generic_params, i)->type, ctx);
                ret = ty_apply_type_arguments(me, ret, (TyList){ .data = args, .len = arg_count });
                free(args);
            }
            return ret;
        }
    }
    // A name that doesn't resolve to a type: an alias that doesn't resolve, a value (TS2749) (tsc's
    // resolveTypeReferenceName makes those unresolved symbols too)
    if (!decl || (decl->is_placeholder && decl->is_unresolved) ||
        (!identifier->keyword && !(decl->flags & SYMBOL_TYPE) && !decl->is_placeholder)
    ) {
        return unresolved_reference(me, identifier, ctx);
    }
    Ty const* args_buf[MAX_TYPE_ARGS];

    if (identifier->this_kind == THIS_CLASS_STATIC) return ty_intrinsic(me, TY_ERROR); // TS2526: no `this` type in statics
    // (An alias is resolved first, which is where a reference inside its own body finds it circular: tsc's
    // getTypeFromTypeAliasReference asks getDeclaredTypeOfSymbol before counting type arguments)
    Ty const* declared = decl->flags & SYMBOL_TYPE_ALIAS ? ty_declared_type_of(me, decl) : nullptr;
    // Too many or too few type arguments (TS2314, TS2315, TS2707) make errorType outside JavaScript (tsc's
    // getTypeFromClassOrInterfaceReference), for a type alias or a type without type parameters in JavaScript too
    // (getTypeFromTypeAliasReference, checkNoTypeArguments)
    if (!identifier->keyword && span_valid(identifier->name) && identifier->target && span_source(identifier->name) &&
        (!span_source(identifier->name)->is_js || (identifier->target->flags & SYMBOL_TYPE_ALIAS) ||
            (!several_types_is_generic(identifier->target) && !identifier_type_is_jsdoc_function(identifier))) &&
        !several_types_arity_fits(identifier->target, (u32)vec_spanned_type_size(&identifier->generic_params), span_source(identifier->name))
    ) {
        return ty_intrinsic(me, TY_ERROR);
    }

    if (!declared) declared = ty_declared_type_of(me, decl);
    if (decl->flags & (SYMBOL_INTERFACE | SYMBOL_CLASS)) {
            // `this` is the class's or interface's own `this` type parameter
            if (identifier->keyword == KEYWORD_IDENTIFIER_THIS && declared->kind == TY_DECLARED && declared->declared.this_param) {
                return declared->declared.this_param;
            }
            if (declared->kind == TY_DECLARED && declared->declared.type_params.len) {
                // TODO: `this` and a bare `C` inside generic class C are the class's own instantiation
                // (type arguments = its type parameters), and `this` is really its own type parameter
                if (identifier->keyword) {
                    if (ctx.generic) return ty_reference(me, declared, declared->declared.type_params);
                }
                TyList args = lower_type_args(
                    me, declared->declared.type_params, declared->declared.outer_count, &identifier->generic_params, ctx, args_buf,
                    identifier_in_js(identifier)
                );
                return reference_from_type_node(me, declared, args);
            }
            return declared;
    }
    if (symbol_is_generic_alias(decl)) {
        TyList params = ty_type_params_of(me, symbol_type_generic(decl));
        TyList args = lower_type_args(me, params, 0, &identifier->generic_params, ctx, args_buf, false);
        if (declared == ty_intrinsic(me, TY_ANY) && args.len == 1) {
            Ty const* intrinsic = intrinsic_alias_instance(me, decl, args.data[0]);
            if (intrinsic) return intrinsic;
        }
        Ty const* ret = alias_instance(me, decl, declared, params, args);
        // Through an import it goes by the alias with the type arguments as written (tsc's getTypeFromTypeAliasReference:
        // the resolved alias of an import/export, aliasTypeArguments from the node, defaults not filled in): it prints
        // as `A` or `A<X>`, not with the defaults
        // (Unless it's the body of another alias, which names it instead: the caller renames it)
        bool own_alias = !(body_of && !(alias_is_local(body_of) && !alias_is_local(decl)));
        bool through_import = identifier->symbol && identifier->symbol != identifier->target && (identifier->symbol->flags & SYMBOL_ALIAS) &&
            own_alias;
        // (tsc's lookup of the name as an alias, which marks an import of that name used: see IdentifierTypeRare)
        if (own_alias && identifier->rare && identifier->rare->args_alias) {
            unused_mark_referenced(identifier->rare->args_alias_scope, identifier->rare->args_alias, SYMBOL_ALIAS);
        }
        TyList written = { .data = args.data, .len = (u32)vec_spanned_type_size(&identifier->generic_params) };
        if (written.len > args.len) written.len = args.len;
        if (through_import && unchanged_composite_body(declared, ret, decl)) {
            // (A union or intersection whose constituents the arguments don't change is the alias's declared type
            // itself, by its own type parameters: tsgo's instantiateTypeWorker returns the type when the members are
            // the same and the alias passed down, the import's resolved one, is its own. Inference then reads its
            // alias type arguments, the parameters themselves)
            ret = declared;
        } else if (through_import && ret->alias == decl && written.len < args.len && !ty_mapped_names_itself(me, ret)) {
            ret = ty_realias(me, ret, decl, written);
        } else if (through_import && ret->alias != decl && (
            (ret->kind == TY_CONDITIONAL && declared->kind == TY_CONDITIONAL && ret->conditional.root == declared->conditional.root) ||
            // (A deferred indexed access too, `type Key<I> = Format<I, 's'>` that is `I[2]`: getIndexedAccessType makes
            // another one by the alias)
            (ret->kind == TY_INDEXED && declared->kind == TY_INDEXED) ||
            // (And a union or intersection, `type JSONSchemaType<T> = Wrapper<"x", Unchecked<T, false>>` that is the
            // union Unchecked<T, false>: tsgo's instantiateTypeWorker makes a union by the import's alias, another type
            // than Unchecked<X, false>. And a deferred reference: getObjectTypeInstantiation keys on the alias. Not yet
            // an instantiated anonymous or mapped type, which tsgo names the same way: realiased, arktype's
            // finalizeTree instantiations ran away into TS5114, see hacks.md)
            ((declared->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) && (
                ((declared->kind == TY_UNION || declared->kind == TY_INTERSECTION) && (ret->kind == TY_UNION || ret->kind == TY_INTERSECTION)) ||
                // (An anonymous one without a name of its own, a function type an indexed access body resolved to:
                // qwik's `FunctionComponent<P> = { renderFn(props: P, ...): JSXOutput }['renderFn']` imported is
                // FunctionComponent<X>, local it prints its signature)
                (declared->kind == TY_ANONYMOUS && !declared->alias && !declared->anonymous.mapped &&
                    ret->kind == TY_ANONYMOUS && !ret->alias) ||
                (declared->kind == TY_REFERENCE && declared->reference.deferred && ret->kind == TY_REFERENCE)
            )) ||
            // (And a distributive conditional type distributed over a union: tsc's getConditionalTypeInstantiation
            // makes the union by the alias passed down, mapTypeWithAlias; yup's `_<MergeObjectTypes<A, B>>`)
            (ret->kind == TY_UNION && distributes_over_union(me, declared, params, args))
        )) {
            // (With type arguments through an import, the alias is passed down: a deferred conditional type, which
            // instantiation names by its root's alias, `Exclude<X, null>` of `import { NonNullable as N }` with
            // `N<U> = Exclude<U, null>`, is N<X>; tsc's getTypeFromTypeAliasReference)
            ret = ty_realias(me, ret, decl, written);
        }
        return ret;
    }
    return ty_regular(me, declared); // `E.x` as a type: an enum member's declared type is fresh
}

// tsc's getInferredTypeParameterConstraint: an `infer X` without `extends` is constrained by where it is written. With
// several declarations (`A<infer X> | B<infer X>`), by all of them
Ty const* ty_implied_infer_constraint(Checker* me, Ty const* param, bool omit_type_args) {
    Symbol const* symbol = param->param.symbol;
    Ty const* found[8];
    u32 len = 0;
    for (u32 i = 0; symbol && i < symbol->decl_count && len < c_arraylen(found); i++) {
        if (symbol->decls[i].kind != SYMBOL_DECL_INFER) continue;
        InferredType const* inferred = symbol->decls[i].node;
        Ty const* constraint = nullptr;
        switch (inferred->position) {
            case INFER_POSITION_OTHER: break;
            case INFER_POSITION_REST: constraint = ty_array(me, ty_intrinsic(me, TY_UNKNOWN_), false); break;
            case INFER_POSITION_TEMPLATE: constraint = ty_intrinsic(me, TY_STRING); break;
            case INFER_POSITION_MAPPED_KEY: constraint = ty_keyof(me, ty_intrinsic(me, TY_ANY)); break;
            case INFER_POSITION_MAPPED_TEMPLATE: {
                // The check type's template with its key's constraint (string | number | symbol without one) for
                // the key
                InterfaceMember const* check = inferred->check_mapped;
                if (!check->key.mapped->owned_symbol) break;
                LowerCtx ctx = { .generic = true };
                Ty const* key = ty_type_param(me, check->key.mapped->owned_symbol);
                Ty const* key_constraint = check->key.mapped->in_type.parsed ? ty_from_node(me, &check->key.mapped->in_type, ctx) : nullptr;
                if (!key_constraint) {
                    Ty const* parts[3] = { ty_intrinsic(me, TY_STRING), ty_intrinsic(me, TY_NUMBER), ty_intrinsic(me, TY_ES_SYMBOL) };
                    key_constraint = ty_union(me, parts, 3, nullptr, (TyList){0});
                }
                Ty const* template_ = ty_from_node(me, &check->type, ctx);
                constraint = ty_instantiate(me, template_, ty_mapper_new(me, (TyList){ .data = &key, .len = 1 },
                    (TyList){ .data = &key_constraint, .len = 1 }));
            }   break;
            case INFER_POSITION_TYPE_ARG: {
                if (omit_type_args) break;
                IdentifierType const* reference = inferred->reference;
                Symbol const* decl = canonical(me, reference->target);
                if (!decl || span_valid(identifier_type_import_span(reference)) || reference->keyword) break;
                TyList params = {0};
                u32 outer = 0;
                if (decl->flags & (SYMBOL_INTERFACE | SYMBOL_CLASS)) {
                    Ty const* declared = ty_declared_type_of(me, decl);
                    if (declared->kind == TY_DECLARED) {
                        params = declared->declared.type_params;
                        outer = declared->declared.outer_count;
                    }
                } else if (symbol_is_generic_alias(decl)) {
                    params = ty_type_params_of(me, symbol_type_generic(decl));
                }
                if (outer + inferred->arg_index >= params.len) break;
                Ty const* declared_constraint = ty_param_constraint(me, params.data[outer + inferred->arg_index]);
                if (!declared_constraint) break;
                // It can mention the other type parameters, which are what the reference passes (`Foo<infer X,
                // infer X>` with `U extends T` gives X itself, which is no constraint)
                Ty const* args_buf[MAX_TYPE_ARGS];
                TyList args = lower_type_args(me, params, outer, &reference->generic_params, (LowerCtx){ .generic = true }, args_buf, false);
                constraint = ty_instantiate(me, declared_constraint, ty_mapper_new(me, params, args));
                if (constraint == param) constraint = nullptr;
            }   break;
        }
        if (constraint) found[len++] = constraint;
    }
    if (len <= 1) return len ? found[0] : nullptr;
    return ty_intersection(me, found, len, nullptr, (TyList){0});
}

// Free type parameters of a node, see Ty.anonymous.free_params

#define MAX_FREE_PARAMS 32 // (Bound ones that are mentioned before they are declared included until the end)
// (Generous: every `infer` of a long conditional chain is one, Playwright's Unboxed has 16)
#define MAX_BOUND_PARAMS 64
typedef struct FreeParams {
    Ty const* params[MAX_FREE_PARAMS];
    u32 len;
    bool unknown;
    // Type parameters declared inside the walked nodes (by a signature, a mapped type, `infer`). They aren't free,
    // and since nothing outside a node can mention what's declared inside it, they're simply taken out at the end
    Ty const* bound[MAX_BOUND_PARAMS];
    u32 bound_len;
    // Inside the extends type of a conditional being walked, which is what declares its `infer` type parameters.
    // Walking a node inside an extends type on its own (`(...args: any) => infer R`), R is free
    u32 in_extends;
    // The true branches (TypeNode.flow) whose extends types were walked already (the extends types' own nodes are in
    // the outer true branches, so a single "last chain" memo would thrash between them)
    // (The first ones inline, the rest in the arena: a long chain of conditionals has a true branch each, opencode's
    // @solid-primitives event maps a chain of 60)
    struct TernaryType const* flow_done[32];
    u32 flow_done_len;
    struct TernaryType const** flow_more;
    u32 flow_more_len, flow_more_cap;
    // The type parameter lists around the Block the nodes are in (TypeFlow.block), which tsc's
    // isTypeParameterPossiblyReferenced takes to be referenced whatever the nodes mention: free_params_add_block()
    struct OuterGenerics const* block;
    // The class or interface declaration the nodes are in (TypeFlow.owner): when it's declared more than once, its type
    // parameters and `this` are free too (tsc's isTypeParameterPossiblyReferenced: more than one declaration)
    Symbol const* owner;
    // A `typeof` of a function's local was bounded by the type parameters around its declarations (typeof_local_params()):
    // the type may still hold a type variable through the local's type, so it could contain one (tsc's
    // couldContainTypeVariables says so of every type literal)
    bool typeof_local;
} FreeParams;

static void free_params_bind(FreeParams* out, Ty const* param) {
    if (out->bound_len == MAX_BOUND_PARAMS) {
        out->unknown = true;
    } else {
        out->bound[out->bound_len++] = param;
    }
}

static void free_params_add(FreeParams* out, Ty const* param) {
    for (u32 i = 0; i < out->len; i++) {
        if (out->params[i] == param) return;
    }
    // (Declared inside the walked nodes already: not free. Only a forward reference, `<A extends B, B>`, is added and
    // taken out at the end)
    for (u32 i = 0; i < out->bound_len; i++) {
        if (out->bound[i] == param) return;
    }
    if (out->len == MAX_FREE_PARAMS) {
        out->unknown = true;
    } else {
        out->params[out->len++] = param;
    }
}

// The type parameters declared around the Block the walked nodes are in (FreeParams.block): free too, whatever the
// nodes mention (tsc's isTypeParameterPossiblyReferenced walks up from the node and gives up at a Block). After the
// walk: none of them is bound in the nodes
static void free_params_add_block(Checker* me, FreeParams* out) {
    if (out->unknown) return;
    if (out->block) {
        TyList around = ty_outer_type_params(me, out->block);
        for (u32 i = 0; i < around.len; i++) free_params_add(out, around.data[i]);
    }
    Ty const* owner = out->owner ? ty_declared_type_of(me, out->owner) : nullptr;
    if (owner && ty_declared_is_merged(owner)) {
        for (u32 i = 0; i < owner->declared.type_params.len; i++) free_params_add(out, owner->declared.type_params.data[i]);
        if (owner->declared.this_param) free_params_add(out, owner->declared.this_param);
    }
}

static void collect_free_params(Checker* me, TypeNode const* node, FreeParams* out);
static void collect_free_params_of_function(Checker* me, FunctionImpl const* function, FreeParams* out);

static void collect_free_params_of_generic(Checker* me, Generic const* generic, FreeParams* out) {
    for_each (i, vec_generic_item, &generic->items) {
        if (i->symbol_ref) free_params_bind(out, ty_type_param(me, i->symbol_ref));
        collect_free_params(me, &i->extends, out);
        if (i->fallback) collect_free_params(me, i->fallback, out);
    }
}

// `typeof a.b.c` where every declaration of `a` is in a scope inside no type parameter list and no function (the top
// level of a file or namespace, or an import): its type mentions no type parameter. A function's locals may, through
// the type parameters of its contextual signature (`f(t => { type A = { x: typeof t } })`), and so may `this` or a name
// declared another way (JavaScript assignments). An instantiation expression's (`typeof f<T>`) type arguments are
// collected by the caller
static bool typeof_mentions_no_type_params(Expr const* first) {
    while (first->tag == EXPR_PROPERTY && !expr_is_perenthisized(first)) first = &first->property->expr;
    if (first->tag != EXPR_IDENTIFIER || first->identifier.keyword || expr_is_perenthisized(first)) return false;
    Symbol const* symbol = first->identifier.target;
    if (!symbol || !symbol->decl_count) return false;
    for (u32 i = 0; i < symbol->decl_count; i++) {
        Scope const* block = symbol->decls[i].block;
        // (An import, `import * as ns`, `import x = N.y`: only in a file or a namespace, never inside what declares a
        // type parameter. JavaScript's `require` aliases can be in a function)
        SymbolDecl const* decl = &symbol->decls[i];
        if (!block && decl->kind == SYMBOL_DECL_ALIAS && span_source(decl->full) && !span_source(decl->full)->is_js) continue;
        if (!block || block->generics || block->function) return false;
    }
    return true;
}

// `typeof x` of a name declared in a function: what it can mention is bounded by the type parameters around x's
// declarations (tsc's isTypeParameterPossiblyReferenced of a type query: a type parameter whose scope has a declaration
// of the name), each of which is around the node too since the name is visible there. A local of a function inside a
// class may mention the class's `this` (tsc's getOuterTypeParameters includes this types): unknown, as is anything but
// a plain name, and so is one inside a context sensitive function expression. xstate's `types: {} as { actors: { logic: typeof child } }` in a non-generic function: no instance per
// inference mapper, the one type tsgo keeps
static bool typeof_local_params(Checker* me, Expr const* first, FreeParams* out) {
    while (first->tag == EXPR_PROPERTY && !expr_is_perenthisized(first)) first = &first->property->expr;
    if (first->tag != EXPR_IDENTIFIER || first->identifier.keyword || expr_is_perenthisized(first)) return false;
    Symbol const* symbol = first->identifier.target;
    if (!symbol || !symbol->decl_count) return false;
    for (u32 i = 0; i < symbol->decl_count; i++) {
        Scope const* block = symbol->decls[i].block;
        if (!block || scope_enclosing_class(block)) return false;
        SourceFile const* source = span_source(symbol->decls[i].full);
        if (!source || source->is_js) return false;
        // A context sensitive function expression around it has its contextual signature's type parameters too
        // (tsc's getOuterTypeParameters; contextualOuterTypeParameters' `f(t => { type A = { x: typeof t } })`)
        for (Scope const* scope = block; scope; scope = scope->parent) {
            if (scope->function && scope->function->is_expression && ty_function_is_context_sensitive(scope->function)) return false;
        }
    }
    for (u32 i = 0; i < symbol->decl_count; i++) {
        TyList around = ty_outer_type_params(me, symbol->decls[i].block->generics);
        for (u32 j = 0; j < around.len; j++) free_params_add(out, around.data[j]);
    }
    out->typeof_local = true;
    return true;
}

static void collect_free_params(Checker* me, TypeNode const* node, FreeParams* out) {
    if (out->unknown) return;
    if (node->flow) {
        // What the node is in the true branches around it can mention their extends types (a substitution type, see
        // conditional_flow_type()), which are outside the node (tsc's isTypeParameterPossiblyReferenced). Mapped type
        // templates add nothing: their key's implied constraint mentions no type parameter
        u32 saved_extends = out->in_extends;
        out->in_extends = 0;
        for (TypeFlow const* flow = node->flow; flow; flow = flow->outer) {
            if (flow->scope == TYPE_FLOW_BLOCK) {
                out->block = flow->block_generics;
                continue;
            }
            if (flow->scope == TYPE_FLOW_OWNER) {
                out->owner = flow->owner;
                continue;
            }
            if (!flow->ternary) continue;
            bool done = false;
            for (u32 i = 0; i < out->flow_done_len && !done; i++) done = out->flow_done[i] == flow->ternary;
            for (u32 i = 0; i < out->flow_more_len && !done; i++) done = out->flow_more[i] == flow->ternary;
            if (done) continue;
            if (out->flow_done_len < c_arraylen(out->flow_done)) {
                out->flow_done[out->flow_done_len++] = flow->ternary;
            } else {
                if (out->flow_more_len == out->flow_more_cap) {
                    u32 cap = out->flow_more_cap ? out->flow_more_cap * 2 : 32;
                    struct TernaryType const** more = arena_alloc(&my arena, sizeof(*more) * cap);
                    if (out->flow_more_len) memcpy(more, out->flow_more, sizeof(*more) * out->flow_more_len);
                    out->flow_more = more;
                    out->flow_more_cap = cap;
                }
                out->flow_more[out->flow_more_len++] = flow->ternary;
            }
            collect_free_params(me, &flow->ternary->maybe_super, out);
        }
        out->in_extends = saved_extends;
    }
    switch (node->tag) {
        case TYPE_ANY:
        case TYPE_UNKNOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_SYMBOL:
        case TYPE_UNIQUE_SYMBOL:
        case TYPE_BIGINT:
        case TYPE_VALUE:
            break;

        case TYPE_IDENTIFIER: {
            IdentifierType const* identifier = node->identifier;
            Symbol const* decl = identifier->target;
            if (identifier->keyword) {
                // `this`: the class's or interface's own `this` type parameter, else (a function's `this`...) unknown
                Ty const* declared = decl && (decl->flags & (SYMBOL_INTERFACE | SYMBOL_CLASS)) &&
                    identifier->keyword == KEYWORD_IDENTIFIER_THIS && identifier->this_kind != THIS_CLASS_STATIC ?
                    ty_declared_type_of(me, decl) : nullptr;
                if (declared && declared->kind == TY_DECLARED && declared->declared.this_param) {
                    free_params_add(out, declared->declared.this_param);
                } else {
                    out->unknown = true;
                }
            } else if (decl && decl->flags & SYMBOL_TYPE_PARAMETER) {
                free_params_add(out, ty_type_param(me, decl));
            } else if (decl && (decl->flags & SYMBOL_TYPE_ALIAS) && symbol_outer_generics(decl)) {
                // A local alias's body can mention the type parameters around it, which it doesn't take as type
                // arguments (tsc's isTypeParameterPossiblyReferenced: a node in a block may reference anything)
                TyList outer = ty_outer_type_params(me, symbol_outer_generics(decl));
                for (u32 i = 0; i < outer.len; i++) free_params_add(out, outer.data[i]);
            }
            for_each (i, vec_spanned_type, &identifier->generic_params) {
                collect_free_params(me, &i->type, out);
            }
        }   break;

        case TYPE_INFERRED:
            if (node->inferred->owned_symbol) {
                Ty const* param = ty_type_param(me, node->inferred->owned_symbol);
                if (out->in_extends) {
                    free_params_bind(out, param);
                } else {
                    free_params_add(out, param);
                }
            }
            collect_free_params(me, &node->inferred->extends, out);
            break;

        case TYPE_CALLABLE:
            collect_free_params_of_function(me, node->callable->function, out);
            break;

        case TYPE_INTERFACE:
            for_each (i, vec_interface_member, &node->interface.members) {
                switch (i->key.tag) {
                    case INTERFACE_KEY_NAME:
                    case INTERFACE_KEY_CALLABLE:
                        break;
                    case INTERFACE_KEY_EXPR:
                        // A computed name in a type has a literal or unique symbol type (TS1170): no type parameter
                        break;
                    case INTERFACE_KEY_INDEX:
                        collect_free_params(me, &i->key.index->type, out);
                        break;
                    case INTERFACE_KEY_MAPPED:
                        if (i->key.mapped->owned_symbol) {
                            free_params_bind(out, ty_type_param(me, i->key.mapped->owned_symbol));
                        }
                        collect_free_params(me, &i->key.mapped->in_type, out);
                        if (i->key.mapped->has_as_type) collect_free_params(me, &i->key.mapped->as_type, out);
                        break;
                }
                collect_free_params(me, &i->type, out);
                // (`get x(): A; set x(v: B)`: the setter's parameter type too)
            }
            break;

        case TYPE_ARRAY:
            collect_free_params(me, node->array.element, out);
            break;

        case TYPE_TUPLE:
            for_each (i, vec_tuple_element, &node->tuple.elements) {
                collect_free_params(me, &i->type, out);
            }
            break;

        case TYPE_INDEXED:
            collect_free_params(me, &node->indexed->interface, out);
            collect_free_params(me, &node->indexed->index, out);
            break;

        case TYPE_KEYOF:
            collect_free_params(me, &node->keyof->interface, out);
            break;

        case TYPE_TERNARY:
            collect_free_params(me, &node->ternary->maybe_sub, out);
            out->in_extends++;
            collect_free_params(me, &node->ternary->maybe_super, out);
            out->in_extends--;
            collect_free_params(me, &node->ternary->if_true, out);
            collect_free_params(me, &node->ternary->if_false, out);
            break;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            for_each (i, vec_type, &node->composite) {
                collect_free_params(me, i, out);
            }
            break;

        case TYPE_TEMPLATE:
            for_each (i, vec_type, &node->template_->types) {
                collect_free_params(me, i, out);
            }
            break;

        case TYPE_JSDOC: collect_free_params(me, &node->jsdoc->type, out); break;
        case TYPE_PREDICATE: break;

        case TYPE_TYPEOF:
            // Not syntax: the type of an expression, which could be about any type parameter in scope. Unless what it
            // names is declared outside every type parameter list (tsc's isTypeParameterPossiblyReferenced: only a name
            // declared inside what declares the type parameter can mention it)
            // `typeof f<T>`: then only what the type arguments mention (the same function's `Some(node.TypeArguments(),
            // containsReference)`)
            if (node->typeof_->expr->tag == EXPR_GENERIC_PARAMS && !expr_is_perenthisized(node->typeof_->expr)) {
                GenericParamsExpr const* applied = node->typeof_->expr->generic_params;
                if (!typeof_mentions_no_type_params(&applied->expr)) {
                    out->unknown = true;
                    break;
                }
                for_each (i, vec_spanned_type, &applied->params) {
                    collect_free_params(me, &i->type, out);
                }
            } else if (!typeof_mentions_no_type_params(node->typeof_->expr) && !typeof_local_params(me, node->typeof_->expr, out)) {
                out->unknown = true;
            }
            break;
    }
}

// A signature written as a type, or an overload signature. A parameter without a type is `any`
// An unannotated parameter whose type is its initializer's, a primitive literal's (`strict = true`), mentions no type
// parameter; an annotated one's initializer doesn't make its type
static bool initializer_mentions_no_type_params(InitializedBinding const* binding) {
    Expr const* value = initialized_binding_value(binding);
    if (value->tag != EXPR_LITERAL) return false;
    switch (value->literal.tag) {
        case VALUE_NULL: case VALUE_BOOLEAN: case VALUE_NUMBER: case VALUE_STRING: case VALUE_BIGINT: return true;
        default: return false;
    }
}

static void collect_free_params_of_function(Checker* me, FunctionImpl const* function, FreeParams* out) {
    if (function->jsdoc_callback) out->unknown = true;
    u32 saved_bound = out->bound_len;
    collect_free_params_of_generic(me, &function->generic, out);
    for_each (i, vec_function_arg, &function->args) {
        if (i->initializer.binding.has_explicit_type) {
            collect_free_params(me, binding_type(&i->initializer.binding), out);
        } else if (initialized_binding_value(&i->initializer)->tag != EXPR_IMPLICIT_ANY && !initializer_mentions_no_type_params(&i->initializer)) {
            // Not syntax: the type of an expression, which could be about any type parameter in scope
            out->unknown = true;
        }
    }
    if (function->has_body && !function->ret_explicit) out->unknown = true;
    collect_free_params(me, &function->ret, out);
    if (function->predicate && function->predicate->has_type) collect_free_params(me, &function->predicate->type, out);
    if (function->predicate && function->predicate->is_this && function->predicate->has_type) {
        // `this is T`, `asserts this is T`: a `this` type node (tsc's isTypeParameterPossiblyReferenced), of the class or
        // interface the signature is in. Its members are then instantiated per `this` (a union receiver's method is
        // a union of them, whose union signature asserts nothing: babel's NodePath.ensureBlock)
        for (TypeFlow const* flow = function->predicate->type.flow; flow; flow = flow->outer) {
            if (flow->scope != TYPE_FLOW_OWNER) continue;
            Ty const* declared = flow->owner ? ty_declared_type_of(me, flow->owner) : nullptr;
            if (declared && declared->kind == TY_DECLARED && declared->declared.this_param) {
                free_params_add(out, declared->declared.this_param);
            }
            break;
        }
    }
    // The signature's own type parameters are in scope nowhere else: out of the lists once it's walked, so the many
    // overloads of a generic method (typesaurus's `field`, 10 of up to 10 type parameters each) don't run out of room
    // and make the type's free parameters unknown (instantiated by the whole mapper: a new type for every mapper)
    // (Only its type parameters: an `infer` in a parameter, `then(f: infer F)` in an extends type, is the conditional's.
    // They were bound first, by collect_free_params_of_generic(), in order)
    if (!out->unknown && vec_generic_item_size(&function->generic.items)) {
        u32 kept_bound = saved_bound;
        u32 next_own = saved_bound;
        for_each (item, vec_generic_item, &function->generic.items) {
            if (!item->symbol_ref) continue;
            Ty const* param = ty_type_param(me, item->symbol_ref);
            u32 kept = 0;
            for (u32 i = 0; i < out->len; i++) if (out->params[i] != param) out->params[kept++] = out->params[i];
            out->len = kept;
            // (What a constraint bound on the way, before this one, stays)
            while (next_own < out->bound_len && out->bound[next_own] != param) out->bound[kept_bound++] = out->bound[next_own++];
            if (next_own < out->bound_len) next_own++;
        }
        while (next_own < out->bound_len) out->bound[kept_bound++] = out->bound[next_own++];
        out->bound_len = kept_bound;
    }
}

// The type parameters from outside that the type of a declared function mentions, when its signature is all
// annotations (every parameter typed, the return type written or nothing to infer it from): what instantiating it
// can change, like Ty.anonymous.free_params of a node. False when they aren't known: an expression (contextually typed),
// JavaScript, a constructor, an accessor or a type read off a body
typedef struct TyFreeParams { TyList params; bool known; } TyFreeParams;

bool ty_function_free_params(Checker* me, FunctionImpl const* function, TyList* out) {
    TyFreeParams const* cached = ty_link_get_memo(me, KEY_FUNCTION_FREE_PARAMS, function);
    if (cached) TAINT_READ(TK_FREE_PARAMS, cached);
    if (!cached) {
        TAINT_BEGIN(taint_frame);
        u64 start = graph_taint_now; // (--shared-graph: published if clean, plan 86 stage 4f)
        TyFreeParams* computed = arena_new(&my arena, TyFreeParams);
        bool eligible = !function->is_expression && !function->is_constructor && !function->accessor &&
            !function->is_type && !function->jsdoc_type && !function->jsdoc_callback && !function->jsdoc_typed &&
            span_source(function->full_span) && !span_source(function->full_span)->is_js && (function->ret_explicit || !function->has_body);
        for_each (i, vec_function_arg, &function->args) {
            if (!i->initializer.binding.has_explicit_type && !initializer_mentions_no_type_params(&i->initializer)) eligible = false;
        }
        if (eligible) {
            FreeParams found = {0};
            collect_free_params_of_function(me, function, &found);
            free_params_add_block(me, &found);
            if (!found.unknown) {
                u32 kept = 0;
                for (u32 i = 0; i < found.len; i++) {
                    bool bound = false;
                    for (u32 j = 0; j < found.bound_len; j++) bound |= found.bound[j] == found.params[i];
                    if (!bound) found.params[kept++] = found.params[i];
                }
                computed->params = ty_list_copy(me, found.params, kept);
                computed->known = true;
            }
        }
        ty_link_set_memo(me, KEY_FUNCTION_FREE_PARAMS, function, computed, start);
        TAINT_END(taint_frame, TK_FREE_PARAMS, computed, function, computed);
        cached = computed;
    }
    *out = cached->params;
    return cached->known;
}

// Only when some type parameter is in scope (ctx.generic), otherwise there's nothing to find
static void anonymous_set_free_params(Checker* me, Ty* ty, FreeParams found) {
    u32 kept = 0;
    for (u32 i = 0; i < found.len; i++) {
        bool bound = false;
        for (u32 j = 0; j < found.bound_len; j++) bound |= found.bound[j] == found.params[i];
        if (!bound) found.params[kept++] = found.params[i];
    }
    found.len = kept;
    bool mentions = kept > 0;
    free_params_add_block(me, &found);
    ty->anonymous.block_params_only = !found.unknown && !mentions && found.len;
    ty->anonymous.free_params_unknown = found.unknown;
    if (!found.unknown) ty->anonymous.free_params = ty_list_copy(me, found.params, found.len);
    if (found.unknown || found.len || found.typeof_local) ty->flags |= TYF_COULD_CONTAIN_TYPE_VARIABLES;
}

static TyConditionalOuter* collect_outer_params(Checker* me, TypeNode const* node) {
    FreeParams found = {0};
    collect_free_params(me, node, &found);
    free_params_add_block(me, &found);
    u32 kept = 0;
    for (u32 i = 0; i < found.len; i++) {
        bool bound = false;
        for (u32 j = 0; j < found.bound_len; j++) bound |= found.bound[j] == found.params[i];
        if (!bound) found.params[kept++] = found.params[i];
    }
    TyConditionalOuter* outer = arena_new(&my arena, TyConditionalOuter);
    outer->unknown = found.unknown;
    if (!found.unknown) outer->params = ty_list_copy(me, found.params, kept);
    return outer;
}

TyConditionalOuter const* ty_root_outer_of_node(Checker* me, Ty const* root, TyRootSpec const* spec, TyList infers) {
    TypeNode const* node = spec->node;
    TyList alias_args = spec->alias_args;
    TyConditionalOuter* outer = collect_outer_params(me, node);
    outer->infers = ty_list_copy(me, infers.data, infers.len);
    // An alias's body keeps the alias's type parameters it doesn't mention too (tsc's getTypeFromConditionalTypeNode
    // filters by isTypeParameterPossiblyReferenced only without an alias): instantiation names a deferred instance by
    // the root's alias with its arguments through the instance's mapper (instantiate.c), `C<X, number>`, not `C<X, U>`
    if (!outer->unknown) {
        TyBuf params = {0};
        for (u32 i = 0; i < outer->params.len; i++) ty_buf_push(&params, outer->params.data[i]);
        for (u32 i = 0; i < alias_args.len; i++) {
            Ty const* arg = alias_args.data[i];
            bool known = arg->kind != TY_TYPE_PARAM;
            for (u32 j = 0; !known && j < params.len; j++) known = params.data[j] == arg;
            if (!known) ty_buf_push(&params, arg);
        }
        if (params.len != outer->params.len) outer->params = ty_list_copy(me, params.data, params.len);
        ty_buf_drop(&params);
    }
    char const* block = node->ternary->enclosing_block;
    Ty const* check = root->conditional.check;
    if (block && check->kind == TY_TYPE_PARAM && check->param.symbol && span_valid(check->param.symbol->name_span)) {
        // (Type parameters are declared before the bodies of what declares them: one declared inside the Block is
        // on a declaration between it and the node)
        Span decl = check->param.symbol->name_span;
        csview text = cstr_sv(&span_source(decl)->contents);
        outer->check_outside_block = block >= text.buf && block < text.buf + text.size && decl.buf < block;
    }
    if (root->conditional.distributive && check->kind == TY_TYPE_PARAM) {
        // (tsc walks up from each branch to the type parameter's declaration: the conditional's own extends type is on
        // the way, `T extends T[] ? number : number`; the flows of the branches add the enclosing ones')
        FreeParams found = {0};
        found.in_extends++;
        collect_free_params(me, &node->ternary->maybe_super, &found);
        found.in_extends--;
        collect_free_params(me, &node->ternary->if_true, &found);
        collect_free_params(me, &node->ternary->if_false, &found);
        free_params_add_block(me, &found);
        bool mentioned = found.unknown;
        for (u32 i = 0; i < found.len && !mentioned; i++) mentioned = found.params[i] == check;
        outer->branches_reference_check = mentioned;
    }
    outer->node = node->ternary->span;
    return outer;
}

// The type parameters declared outside a deferred reference's node that it mentions: what its instantiations are
// keyed by (tsc's getObjectTypeInstantiation keys by the outer type parameters the node references)
static TyConditionalOuter const* deferred_node_outer(Checker* me, TypeNode const* node) {
    u64 key[2] = { KEY_DEFERRED_OUTER, (u64)(uintptr_t)node };
    TyConditionalOuter const* ret = intern_get(&my links, key, 2);
    if (ret) return ret;
    TyConditionalOuter* outer = collect_outer_params(me, node);
    return intern_set(me, &my links, intern_slot(me, &my links, key, 2), outer);
}

TyConditionalOuter const* ty_conditional_outer(Checker* me, Ty const* root) {
    (void)me;
    return ty_root_outer(root);
}

Ty const* ty_anonymous_from_node(Checker* me, TypeNode const* node, LowerCtx ctx, Symbol const* alias, TyList alias_args) {
    check(node->tag == TYPE_INTERFACE || node->tag == TYPE_CALLABLE);
    ctx.in_alias = false; // Members are resolved lazily, which is what makes recursion through them legal
    u64 key[2] = { KEY_NODE, (u64)(uintptr_t)node };
    Ty const* existing = intern_get(&my links, key, 2);
    if (existing) return existing;

    Ty* ret = ty_alloc(me, TY_ANONYMOUS, TYF_OBJECT);
    ret->anonymous.node = node;
    ret->anonymous.mapped = node->tag == TYPE_INTERFACE && vec_interface_member_size(&node->interface.members) == 1 &&
        node->interface.members.data[0].key.tag == INTERFACE_KEY_MAPPED;
    ret->anonymous.ctx = ctx;
    ret->anonymous.state = arena_new(&my arena, TyObjectState);
    ret->alias = alias;
    ret->alias_args = alias_args;
    if (ctx.generic) {
        FreeParams found = {0};
        collect_free_params(me, node, &found);
        // An alias's body keeps the alias's type parameters it doesn't mention too (tsc's getObjectTypeInstantiation
        // keeps every outer type parameter of a type with alias arguments): `type H<A, B = Option<A>> = (b: B) => void`
        // instantiated by A prints H<U, Option<U>>
        for (u32 i = 0; i < alias_args.len && !found.unknown; i++) {
            Ty const* arg = alias_args.data[i];
            bool known = arg->kind != TY_TYPE_PARAM;
            for (u32 j = 0; !known && j < found.len; j++) known = found.params[j] == arg;
            if (known) continue;
            if (found.len == MAX_FREE_PARAMS) found.unknown = true;
            else found.params[found.len++] = arg;
        }
        anonymous_set_free_params(me, ret, found);
    }
    // Collecting can intern type parameters, so the slot is looked up again (an identity: with --shared-graph
    // published at once, intern_set())
    return intern_set(me, &my links, intern_slot(me, &my links, key, 2), ret);
}

Ty const* ty_anonymous_from_overloads(Checker* me, FunctionImpl const* const* overloads, u32 count, LowerCtx ctx) {
    ctx.in_alias = false;
    u64 key[2] = { KEY_NODE, (u64)(uintptr_t)overloads };
    Ty const* existing = intern_get(&my links, key, 2);
    if (existing) return existing;

    Ty* ret = ty_alloc(me, TY_ANONYMOUS, TYF_OBJECT);
    ret->anonymous.overloads = overloads;
    ret->anonymous.overload_count = count;
    ret->anonymous.ctx = ctx;
    ret->anonymous.state = arena_new(&my arena, TyObjectState);
    if (ctx.generic) {
        FreeParams found = {0};
        for (u32 i = 0; i < count; i++) collect_free_params_of_function(me, overloads[i], &found);
        anonymous_set_free_params(me, ret, found);
    }
    return intern_set(me, &my links, intern_slot(me, &my links, key, 2), ret);
}

static Ty* make_namespace(Checker* me, Symbol const* symbol) {
    Ty* ret = ty_alloc(me, TY_DECLARED, TYF_OBJECT);
    ret->declared.kind = TY_DECLARED_NAMESPACE;
    ret->declared.namespace_symbol = symbol;
    if (symbol->is_module) {
        // `typeof import("a")`. `import * as ns` has a symbol of its own (make_gather_target())
        cstr module_name = module_name_for_error(symbol, nullptr, false);
        cstr name = cstr_d("import(", module_name, ")");
        ret->declared.name = checker_atom(me, cstr_sv(&name));
        cstr_drop(&name);
        cstr_drop(&module_name);
    } else {
        ret->declared.name = ty_qualified_name(me, symbol);
    }
    ret->declared.state = arena_new(&my arena, TyObjectState);
    return ret;
}

static Ty const* own_type_of_symbol(Checker* me, Symbol const* symbol);

Ty const* ty_commonjs_exports_type(Checker* me, SourceFile const* file) {
    Symbol* module = file && file->scope ? file->scope->container : nullptr;
    if (!module) return ty_intrinsic(me, TY_ANY);
    Symbol* assigned = module_export_equals(module);
    Ty const* exports;
    if (assigned != module) {
        exports = ty_type_of_symbol(me, assigned);
        // Nothing but null, undefined or never assigned is any in JavaScript (tsc's getTypeOfFuncClassEnumModule through
        // getWidenedTypeForAssignmentDeclaration of `export=`, which reports it: TS7008 in expr.c)
        if (file->is_js && !(exports->flags & TYF_ANY) && ty_only_nullable(exports)) exports = ty_intrinsic(me, TY_ANY);
    } else if (symbol_table_get(module->exports, c_sv("export="))) {
        // `module.exports = window.nonprop`: an alias that resolves to nothing (tsc's unknownSymbol). One of the
        // module itself (`module.exports = exports` in a function) is the module's namespace, `typeof import("m")`
        bool itself = symbol_resolve_alias(symbol_table_get(module->exports, c_sv("export="))) == module;
        exports = itself ? ty_namespace_type(me, module) : ty_intrinsic(me, TY_ERROR);
    } else {
        exports = ty_namespace_type(me, module);
    }
    return exports;
}

// The `module` of a CommonJS file: `{ exports: <what the module exports> }` (tsc builds it in
// getTypeOfVariableOrParameterOrPropertyWorker for SymbolFlags.ModuleExports)
Ty const* ty_commonjs_module_object(Checker* me, SourceFile const* file) {
    Symbol* module = file->scope ? file->scope->container : nullptr;
    if (!module) return ty_intrinsic(me, TY_ANY);
    Span* file_span = arena_new(&my arena, Span);
    *file_span = span_from_sv(file, csview_from_n(source_file_first_token(file).buf, 0));
    Ty const* exports = ty_commonjs_exports_type(me, file);
    ObjectLiteralProp prop = {
        .name = checker_atom(me, c_sv("exports")),
        .type = exports,
        .name_span = *file_span,
        .decl_spans = file_span,
        .decl_span_count = 1,
    };
    // (Its symbol is `module`'s, SymbolFlags.ModuleExports: no inferable index, tsc's isObjectTypeWithInferableIndex)
    return ty_object_literal(me, &prop, 1, nullptr, 0, OBJECT_SYNTHETIC | OBJECT_NO_INFERABLE_INDEX);
}

// The value of a namespace (or of a module, through `import * as ns`): a property per value it declares
static bool is_js_container(Symbol const* symbol, Expr const* value);

// Whether symbol's value declaration makes it a JavaScript container (see value_entry_type())
static bool symbol_is_js_container(Symbol const* symbol) {
    if (!symbol->exports || !symbol->decl_count) return false;
    SymbolDecl const* decl = &symbol->decls[0];
    if (decl->kind == SYMBOL_DECL_ASSIGNMENT) return is_js_container(symbol, decl->node);
    if (decl->kind != SYMBOL_DECL_VARIABLE) return false;
    Binding const* binding = decl->node;
    return !binding->parent && binding->tag == BINDING_IDENTIFIER && !binding->has_explicit_type &&
        is_js_container(symbol, initialized_binding_value(((InitializedBinding const*)binding)));
}

Ty const* ty_namespace_type(Checker* me, Symbol const* symbol) {
    u64 key[2] = { KEY_NAMESPACE, (u64)(uintptr_t)symbol };
    Ty const* existing = intern_get(&my links, key, 2);
    if (existing) return existing;
    Ty* ret = make_namespace(me, symbol);
    ret->declared.js_container = symbol_is_js_container(symbol);
    return intern_set(me, &my links, intern_slot(me, &my links, key, 2), ret);
}

// Whether a declaration of a symbol adds something to what the symbol is as a value
// tsc's isAliasableExpression: an entity name (`undefined` too) or a class expression
static bool is_aliasable_expression(Expr const* value) {
    if (!value || expr_is_perenthisized(value)) return false;
    if (value->tag == EXPR_LITERAL) return value->literal.tag == VALUE_UNDEFINED && csview_equals(span_sv(value->inner_span), "undefined");
    return value->tag == EXPR_CLASS_DECL || expr_is_entity_name(value);
}

// A CommonJS export whose declarations are all `exports.x = <aliasable expression>` (tsc: an Alias symbol)
static bool commonjs_alias_export(Symbol const* symbol) {
    for (u32 i = 0; i < symbol->decl_count; i++) {
        SymbolDecl const* decl = &symbol->decls[i];
        // (tttt binds `exports.x = f` as an alias of its own, `exports.x = undefined` as an assignment)
        if (decl->kind == SYMBOL_DECL_ALIAS) continue;
        if (decl->kind != SYMBOL_DECL_ASSIGNMENT || decl->assignment != ASSIGNMENT_DECL_PROPERTY) return false;
        if (!span_source(decl->full) || !span_source(decl->full)->is_js || !is_aliasable_expression(decl->node)) return false;
    }
    return true;
}

bool ty_is_duplicated_commonjs_alias_export(Symbol const* symbol) {
    return symbol && symbol->decl_count > 1 && symbol->parent && symbol->parent->is_module && commonjs_alias_export(symbol);
}

static bool decl_contributes_value(Symbol const* symbol, u32 index) {
    SymbolDecl const* decl = &symbol->decls[index];
    if (!(decl->flags & SYMBOL_VALUE)) return false;
    u32 once = 0; // Of these only the first counts
    switch (decl->kind) {
        case SYMBOL_DECL_VARIABLE:
        case SYMBOL_DECL_BUILTIN:
        case SYMBOL_DECL_EXPR:
        case SYMBOL_DECL_SOURCE_PHASE:
            once = SYMBOL_VARIABLE; // `var a; var a;`, a parameter and a `var`: the first one gives the type. TODO: TS2403
            break;
        case SYMBOL_DECL_FUNCTION:
            // The implementation of overloads isn't a signature: an implementation right after another declaration
            // of the function (tsc's getSignaturesOfSymbol: same parent and kind, nothing but trivia between). Others,
            // like duplicate implementations in two files, are signatures of their own
            if (((FunctionImpl const*)decl->node)->has_body && index > 0) {
                SymbolDecl const* previous = &symbol->decls[index - 1];
                if (previous->kind != SYMBOL_DECL_FUNCTION || span_source(previous->full) != span_source(decl->full) ||
                    !span_valid(previous->full) || !span_valid(decl->full)
                ) {
                    return true;
                }
                char const* between = span_end_ptr(previous->full);
                char const* start = span_start_ptr(decl->full);
                if (between > start) return true;
                while (between < start) {
                    if (*between == ' ' || *between == '\t' || *between == '\r' || *between == '\n' || *between == ';') {
                        between++;
                    } else if (between[0] == '/' && between[1] == '/') {
                        while (between < start && *between != '\n') between++;
                    } else if (between[0] == '/' && between[1] == '*') {
                        char const* close = strstr(between + 2, "*/");
                        between = close ? close + 2 : start;
                    } else {
                        return true;
                    }
                }
                return false;
            }
            break;
        case SYMBOL_DECL_ENUM: once = SYMBOL_ENUM; break; // One object for every declaration of the enum
        case SYMBOL_DECL_NAMESPACE: once = SYMBOL_VALUE_MODULE; break; // And one for every block of a namespace
        case SYMBOL_DECL_ASSIGNMENT:
            // An expando of a namespace's variable (`namespace N { export var x }` and `N.x = 1`) is assigned to it
            for (u32 i = 0; i < symbol->decl_count; i++) {
                SymbolDeclKind kind = symbol->decls[i].kind;
                if (kind != SYMBOL_DECL_ASSIGNMENT && kind != SYMBOL_DECL_EXPANDO && (symbol->decls[i].flags & SYMBOL_VALUE)) return false;
            }
            break;
        case SYMBOL_DECL_EXPANDO:
            // A variable's or an assignment's expandos are members of its initializer
            if (symbol->flags & SYMBOL_VARIABLE) return false;
            for (u32 i = 0; i < symbol->decl_count; i++) {
                if (symbol->decls[i].kind == SYMBOL_DECL_ASSIGNMENT) return false;
            }
            once = SYMBOL_VALUE_MODULE;
            break;
        default: break;
    }
    if (!once) return true;
    for (u32 i = 0; i < index; i++) {
        if (symbol->decls[i].flags & once) return false;
    }
    return true;
}

// `const f: T = () => {}; f.x = v`: an expando of an annotated variable has the type the annotation gives its name
// (tsc's getAnnotatedTypeForAssignmentDeclaration), null if the annotation doesn't have it
static Ty const* annotated_expando_type(Checker* me, Symbol const* symbol) {
    Symbol const* host = symbol->parent;
    if (!host || host->is_module || !(host->flags & SYMBOL_VARIABLE)) return nullptr;
    Binding const* binding = symbol_find_node(host, SYMBOL_DECL_VARIABLE);
    if (!binding || binding->parent || binding->tag != BINDING_IDENTIFIER || !binding->has_explicit_type) return nullptr;
    Ty const* annotation = ty_binding_type(me, binding, (LowerCtx){ .generic = true });
    // (An optional property's type is read with its undefined, but not exactOptionalPropertyTypes' missing type: tsc's
    // getNonMissingTypeOfSymbol). A late-bound expando (`f[sym] = v`) by its key's type
    Expr const* late_bound_key = symbol_rare(symbol)->late_bound_key;
    // (A property, not what an index signature says, and `never` has none: tsc's getPropertyOfType)
    if (!late_bound_key) {
        Ty const* apparent = ty_apparent_type(me, annotation);
        if (apparent->kind != TY_UNION && !ty_find_prop(me, apparent, checker_atom(me, symbol->name)).found) return nullptr;
    }
    Ty const* type = ty_access(me, annotation, late_bound_key ? ty_expr_type(me, late_bound_key) : ty_string_literal(me, symbol->name));
    return type ? ty_remove_missing(me, type, true) : nullptr;
}

// What a declaring assignment (`exports.x = v`, `f.x = v`) assigns, before widening object literals (tsc's
// getInitializerTypeFromAssignmentDeclaration): an export keeps its literal type, an expando's is widened
// `Object.defineProperty(o, "x", descriptor)`: the descriptor's `value`, else its getter's return type, else its setter's
// parameter type, else any (tsc's getInitializerTypeFromAssignmentDeclaration for a call)
static Ty const* define_property_type(Checker* me, Expr const* call) {
    Ty const* descriptor = ty_expr_type(me, vec_expr_at(&call->call->args, 2));
    Ty const* value = ty_prop_type_of(me, descriptor, my atoms.value);
    if (value) return value;
    Ty const* get = ty_prop_type_of(me, descriptor, checker_atom(me, c_sv("get")));
    Signature const* get_sig = get ? ty_single_signature(me, get, false, true) : nullptr;
    if (get_sig) return signature_return_type(me, get_sig);
    Ty const* set = ty_prop_type_of(me, descriptor, checker_atom(me, c_sv("set")));
    Signature const* set_sig = set ? ty_single_signature(me, set, false, true) : nullptr;
    if (set_sig) {
        SigResolved const* set_sig_res = signature_resolve(me, set_sig);
        return set_sig_res->param_count ? ty_signature_param_type_at(me, set_sig, 0) : ty_intrinsic(me, TY_NEVER);
    }
    return ty_intrinsic(me, TY_ANY);
}

// tsc's isEmptyArrayLiteralType: the type of `[]` (its element type is implicitNeverType, or undefinedWideningType
// without strictNullChecks), which a JavaScript declaration widens to any[] (widenTypeInferredFromInitializer)
bool ty_is_empty_array_literal_type(Checker* me, Ty const* type) {
    if (type->kind != TY_REFERENCE || !ty_is_array(me, type)) return false;
    TyList args = ty_reference_args(me, type);
    return args.len == 1 && args.data[0] == ty_intrinsic(me, compiler_options()->strict_null_checks ? TY_IMPLICIT_NEVER : TY_UNDEFINED_WIDENING);
}

static Ty const* assignment_declaration_type(Checker* me, Symbol const* symbol, SymbolDecl const* decl) {
    if (decl->assignment == ASSIGNMENT_DECL_DEFINE_PROPERTY) return define_property_type(me, decl->node);
    // A declaration that assigns nothing (`/** @type {T} */ this["x"];` whose tag isn't its): never, so any in the end
    // (tsc's getWidenedTypeForAssignmentDeclaration pushes neverType for it)
    Expr const* node = decl->node;
    if (node && node->tag == EXPR_PROPERTY && span_eq(expr_outer_span(node), decl->full)) return ty_intrinsic(me, TY_NEVER);
    // `f[k] = function () {}`: a late-bound member's value is contextually typed by the access it is assigned to (tsc's
    // getContextualTypeForAssignmentDeclaration: the left has no symbol, getTypeOfExpression of it), which is this
    // member's type again: circular, any (TS7022 under noImplicitAny). A value that asks for no contextual type (a
    // literal) types it. tsgo gives none to `F[k] = v` that the binder declared (a TS expando): typed like any other
    // (PROTOTYPE: JavaScript keeps 6.0's until todo entry 8)
    // (An annotated variable's expando is contextually typed by what the annotation says the member is, its left side)
    Ty const* contextual = annotated_expando_type(me, symbol);
    if (!contextual && symbol_rare(symbol)->late_bound_key && node && span_source(decl->full) && span_source(decl->full)->is_js && decl->assignment != ASSIGNMENT_DECL_THIS_IN_CONSTRUCTOR &&
        decl->assignment != ASSIGNMENT_DECL_THIS_IN_METHOD
    ) {
        Expr const* value = node;
        if (value->tag == EXPR_FUNCTION || value->tag == EXPR_OBJECT || value->tag == EXPR_ARRAY) {
            if (compiler_options()->no_implicit_any && span_valid(decl->full) && span_valid(decl->name)) {
                // (At the whole assignment, tsc's declaration)
                Span value_span = expr_outer_span(value);
                Span whole = span_make_in(decl->full, span_start_ptr(decl->full), span_end_ptr(value_span));
                ty_error_at(me, span_end_ptr(value_span) > span_start_ptr(decl->full) ? whole : decl->full, 7022, cstr_d("'", decl->name, "' implicitly has type 'any' because it does not have a "
                    "type annotation and is referenced directly or indirectly in its own initializer."));
            }
            return ty_intrinsic(me, TY_ANY);
        }
    }
    Ty const* type = contextual ? ty_check_expr(me, decl->node, contextual) : ty_expr_type(me, decl->node);
    // `[]` is any[] in JavaScript, which noImplicitAny reports (tsc's getInitializerTypeFromAssignmentDeclaration:
    // isEmptyArrayLiteralType, see ty_js_assignment_implicit_any())
    // (In TypeScript too, an expando of a function: unless the function's variable is annotated, tsc's
    // hasParentWithTypeAnnotation)
    SourceFile const* file = span_source(decl->full);
    Binding const* parent_binding = symbol->parent ? symbol_find_node(symbol->parent, SYMBOL_DECL_VARIABLE) : nullptr;
    if (file && (file->is_js || !parent_binding || !parent_binding->has_explicit_type) && ty_is_empty_array_literal_type(me, type)) {
        TY_WORKER_STATE(me);
        *intern_slot(me, &my links, (u64[]){ KEY_JS_IMPLICIT_ANY, (u64)(uintptr_t)decl->node }, 2) = decl->node;
        return ty_array(me, ty_intrinsic(me, TY_ANY), false);
    }
    // (A direct export keeps its literal type, tsc's isDirectExport; a global declared by `this.x = v` doesn't)
    // (Otherwise as a mutable location: unique symbols widen too, tsgo's checkExpressionForMutableLocation)
    bool direct = symbol->parent && symbol->parent->is_module;
    // (An expando of an annotated variable: its value is contextually typed by what the annotation says the member is,
    // whose literals it keeps, tsc's checkExpressionForMutableLocation; its type is still the value's)
    Ty const* annotated = direct ? nullptr : annotated_expando_type(me, symbol);
    if (annotated && ty_is_literal_of_contextual_type(me, type, annotated)) return ty_regular(me, type);
    return direct ? ty_regular(me, type) : ty_widened_unique_symbol(me, ty_widened_literal(me, type));
}

// JavaScript `var ns = {}` or `ns.a = {}` with expandos (`ns.x = 1`): the members they declare (tsc's
// getJSContainerObjectType). `value` is the initializer or assigned value
static bool is_js_container(Symbol const* symbol, Expr const* value) {
    if (!symbol->exports || !value) return false;
    SourceFile const* file = span_source(value->inner_span);
    if (!file || !file->is_js) return false;
    Expr const* init = expando_initializer((Expr*)value, true, false);
    return init && init->tag == EXPR_OBJECT;
}

// The type of the property a SYMBOL_DECL_EXPORT_EQUALS_PROPERTY stands for, null if what `export =` names has none
static Ty const* export_equals_property_type_of(Checker* me, Symbol const* symbol) {
    Ty const* assigned = ty_type_of_symbol(me, symbol->decls[0].node);
    Atom const* name = checker_atom(me, symbol->name);
    // (Of a union, a property every member has: tsc's getPropertyOfType makes a union property, without
    // Object's members, skipObjectFunctionPropertyAugment)
    if (assigned->kind == TY_UNION && !(assigned->flags & TYF_BOOLEAN)) {
        TyList members = assigned->composite.types;
        Ty const** types = malloc(sizeof(Ty const*) * members.len);
        panic_if(!types, "out of memory");
        u32 count = 0;
        for (u32 i = 0; i < members.len; i++) {
            Ty const* part = ty_prop_type_of(me, members.data[i], name);
            if (!part) break;
            types[count++] = part;
        }
        Ty const* ret = count == members.len ? ty_union(me, types, count, nullptr, (TyList){0}) : nullptr;
        free(types);
        return ret;
    }
    return ty_prop_type_of(me, assigned, name);
}

Symbol const* ty_alias_combined_type(Checker* me, Symbol const* alias) {
    if (!alias || !(alias->flags & SYMBOL_ALIAS) || !alias->alias || !alias->alias->combined_type) return nullptr;
    return ty_export_equals_property_missing(me, alias->alias->value_target) ? nullptr : alias->alias->combined_type;
}

bool ty_export_equals_property_missing(Checker* me, Symbol const* property) {
    return !export_equals_property_type_of(me, property);
}

// `export default e`: resolved like tsc's getTypeOfVariableOrParameterOrProperty pushing the symbol, which e reaching
// again (`import D from "./self"; export default { m: [D] }`) makes any, TS7022 at the statement. The expression keeps
// its own type; the declaration has no memo of its own, so a circular one is remembered
static Ty const* export_expression_type(Checker* me, Symbol const* symbol, SymbolDecl const* decl) {
    Ty const* circular = ty_intrinsic(me, TY_CIRCULAR);
    u64 key[2] = { KEY_CIRCULAR_BINDING, (u64)(uintptr_t)decl };
    if (my circular_bindings && intern_get(&my links, key, 2)) {
        TY_WORKER_STATE(me); // (A recorded cycle: this worker's)
        return circular;
    }
    if (ty_resolving_find(me, RESOLVING_DECLARATION, decl)) {
        ty_resolving_cycle(me, RESOLVING_DECLARATION, decl);
        return circular;
    }
    ty_resolving_push(me, RESOLVING_DECLARATION, decl);
    Ty const* type = ty_regular(me, ty_widened_objects(me, ty_expr_type(me, decl->node)));
    if (ty_resolving_pop(me)) {
        my circular_bindings = true;
        *intern_slot(me, &my links, key, 2) = (void*)decl;
        if (compiler_options()->no_implicit_any && span_valid(decl->full)) {
            ty_error_at(me, decl->full, 7022, cstr_d("'", symbol->name, "' implicitly has type 'any' because it does not "
                "have a type annotation and is referenced directly or indirectly in its own initializer."));
        }
        return circular;
    }
    return type;
}

static Ty const* value_entry_type(Checker* me, Symbol const* symbol, SymbolDecl const* decl) {
    switch (decl->kind) {
        case SYMBOL_DECL_VARIABLE: {
            Binding const* binding = decl->node;
            if (!binding->parent && binding->tag == BINDING_IDENTIFIER && !binding->has_explicit_type &&
                is_js_container(symbol, initialized_binding_value(((InitializedBinding const*)binding)))
            ) {
                return ty_namespace_type(me, symbol);
            }
            Ty const* type = ty_binding_type(me, binding, (LowerCtx){ .generic = true });
            // `x?: T` is `T | undefined` (tsc's getTypeForVariableLikeDeclaration addOptionality; its signature's
            // SigParam.type has it too)
            if (binding->optional_parameter && compiler_options()->strict_null_checks) {
                // (An unresolved name's error type with undefined is the plain error type: tsc's getUnionType)
                if (type->flags & TYF_ANY) return type->flags & TYF_ERROR ? ty_intrinsic(me, TY_ERROR) : type;
                Ty const* types[2] = { type, ty_intrinsic(me, TY_UNDEFINED) };
                type = ty_union(me, types, 2, nullptr, (TyList){0});
            }
            return type;
        }
        case SYMBOL_DECL_BUILTIN: return ty_from_node(me, decl->node, (LowerCtx){0});
        // `export default e` (exportDefaultStripsFreshness)
        case SYMBOL_DECL_EXPR:
            // (A JSDoc `@type` on it is its type: tsc's getEffectiveTypeAnnotationNode of an ExportAssignment)
            if (symbol_decl_jsdoc_type(decl)) return ty_of_node(symbol_decl_jsdoc_type(decl));
            // A JSON file's value is widened, literals too (getTypeOfVariableOrParameterOrPropertyWorker)
            if (span_source(decl->full) && span_source(decl->full)->is_json) {
                // (An empty file, parsed as an empty `{}` there, is tsc's emptyObjectType: no symbol, so no inferable
                // index, `Record<string, unknown>` needs one)
                Expr const* value = decl->node;
                if (value->tag == EXPR_OBJECT && !value->inner_span.size && !vec_object_expr_item_size(&value->object)) {
                    return ty_empty_object_type(me);
                }
                return ty_widened_for_declaration(me, ty_expr_type(me, decl->node), true);
            }
            // An alias of `a.b` that names no symbol (entity_value_symbol()): what checking it resolved, which for
            // a property of any is nothing (tsc's getTargetOfAliasLikeExpression: unknownSymbol, errorType)
            if (symbol->alias == nullptr && ((Expr const*)decl->node)->tag == EXPR_PROPERTY && expr_is_entity_name(decl->node) &&
                !expr_is_perenthisized(decl->node) &&
                (ty_expr_type(me, &((Expr const*)decl->node)->property->expr)->flags & TYF_ANY)
            ) {
                return ty_intrinsic(me, TY_ERROR);
            }
            // (A name, an alias to tsc, is its symbol's type, not the flow type there: `export default v` in a function
            // after `var v;` is any, see declare_function_export(); a file's or namespace's is an alias declaration)
            if (((Expr const*)decl->node)->tag == EXPR_IDENTIFIER && !expr_is_perenthisized(decl->node) &&
                ((Expr const*)decl->node)->identifier.target && !((Expr const*)decl->node)->identifier.target->is_placeholder
            ) {
                return ty_type_of_symbol(me, ((Expr const*)decl->node)->identifier.target);
            }
            return export_expression_type(me, symbol, decl);
        case SYMBOL_DECL_ASSIGNMENT:
            // tsgo's getAssignmentDeclarationInitializerType: `module.exports = 1` exports `1` (getWidenedType only widens
            // object literals and, without strictNullChecks, null and undefined)
            if (csview_equals(symbol->name, "export=")) {
                if (symbol_decl_jsdoc_type(decl)) return ty_of_node(symbol_decl_jsdoc_type(decl));
                return ty_widened_nullable(me, ty_widened_objects(me, ty_regular(me, ty_expr_type(me, decl->node))));
            }
            if (is_js_container(symbol, decl->node)) return ty_namespace_type(me, symbol);
            // (An annotated variable's expando: the value as contextually typed by the annotation, its literals kept.
            // An object literal is widened all the same, no longer fresh: tsc's getWidenedTypeForAssignmentDeclaration
            // ends with getWidenedType, so `f.args = { x: 1 }` against a weak `args` type is "no properties in common")
            if (annotated_expando_type(me, symbol)) {
                return ty_widened_objects(me, ty_regular(me, assignment_declaration_type(me, symbol, decl)));
            }
            {
                Ty const* type = ty_widened_objects(me, assignment_declaration_type(me, symbol, decl));
                // Nothing but null and undefined is any in JavaScript (tsc's getWidenedTypeForAssignmentDeclaration),
                // except `= undefined`, an alias of undefined to tsc (isAliasableExpression)
                Expr const* value = decl->node;
                SourceFile const* file = span_source(decl->full);
                bool undefined_alias = value->tag == EXPR_LITERAL && value->literal.tag == VALUE_UNDEFINED;
                // (After getWidenedType: without strictNullChecks what widens is any already, nothing to report)
                if (file && file->is_js && !undefined_alias && decl->assignment != ASSIGNMENT_DECL_DEFINE_PROPERTY &&
                    ty_only_nullable(ty_widened_nullable(me, type))
                ) {
                    TY_WORKER_STATE(me);
                    *intern_slot(me, &my links, (u64[]){ KEY_JS_IMPLICIT_ANY, (u64)(uintptr_t)symbol }, 2) = (void const*)(uintptr_t)2;
                    return ty_intrinsic(me, TY_ANY);
                }
                // (tsc's getWidenedType: without strictNullChecks `Foo.x = []` is any[]; `= undefined` is an alias)
                return undefined_alias ? type : ty_widened_nullable(me, type);
            }
        case SYMBOL_DECL_COMMONJS_MODULE: return ty_commonjs_module_object(me, decl->node);
        case SYMBOL_DECL_COMMONJS_EXPORTS: return ty_commonjs_exports_type(me, decl->node);
        case SYMBOL_DECL_FUNCTION: return ty_function_type(me, decl->node);
        case SYMBOL_DECL_CLASS: {
            // A local class in a generic scope: applied to the outer type parameters, so that instantiating it with
            // the function's mapper instantiates its statics
            Ty const* static_type = ty_static_type_of(me, symbol);
            TyList outer = static_type->declared.type_params;
            Ty const* value = outer.len ? ty_reference(me, static_type, outer) : static_type;
            // A mixin class is also what it extends: `typeof C & T` (tsc's getTypeOfFuncClassEnumModule)
            Ty const* variable = ty_class_base_type_variable(me, symbol);
            if (variable) {
                Ty const* parts[2] = { value, variable };
                value = ty_intersection(me, parts, 2, nullptr, (TyList){0});
            }
            return value;
        }
        case SYMBOL_DECL_ENUM:
            return ty_static_type_of(me, symbol);
        case SYMBOL_DECL_ENUM_MEMBER: return ty_declared_type_of(me, symbol);
        case SYMBOL_DECL_NAMESPACE:
        case SYMBOL_DECL_EXPANDO:
        case SYMBOL_DECL_ALIAS: // `import * as ns`
            return ty_namespace_type(me, symbol);
        case SYMBOL_DECL_EXPORT_EQUALS_PROPERTY: {
            Ty const* type = export_equals_property_type_of(me, symbol);
            return type ? type : ty_intrinsic(me, TY_ERROR);
        }
        case SYMBOL_DECL_SOURCE_PHASE: return ty_abstract_module_source_type(me);
        default:
            return ty_intrinsic(me, TY_ERROR);
    }
}

// What a symbol is as a value, one type per declaration that contributes to it. malloc'd
static u32 value_entries(Checker* me, Symbol const* symbol, Ty const*** out) {
    u32 count = 0;
    *out = malloc(sizeof(Ty const*) * (symbol->decl_count + 1));
    panic_if(!*out, "out of memory");
    // (A module a UMD global's `declare global` function merged into, merge_into_umd_globals(): its exports first)
    if (symbol->is_module) (*out)[count++] = ty_namespace_type(me, symbol);
    for (u32 i = 0; i < symbol->decl_count; i++) {
        if (decl_contributes_value(symbol, i)) (*out)[count++] = value_entry_type(me, symbol, &symbol->decls[i]);
    }
    return count;
}

// The properties of an intersection as tsc's getPropertiesOfType lists them: those of all of its members, each name
// once. One only a single member has is that member's (tsc's createUnionOrIntersectionProperty returns the single
// symbol), others of the type and with the modifiers the intersection has for them. malloc'd
static u32 intersection_props(Checker* me, Ty const* ty, ObjectLiteralProp** out) {
    u32 count = 0, cap = 0;
    *out = nullptr;
    for (u32 p = 0; p < ty->composite.types.len; p++) {
        Ty const* part = ty_apparent_type(me, ty->composite.types.data[p]);
        if (!(part->flags & TYF_OBJECT)) continue;
        Members const* members = ty_members(me, part);
        for (u32 i = 0; i < members->prop_count; i++) {
            Prop const* prop = members->props[i];
            bool seen = false;
            for (u32 k = 0; k < count && !seen; k++) seen = (*out)[k].name == prop->name;
            if (seen) continue;
            u32 owners = 0;
            for (u32 q = 0; q < ty->composite.types.len; q++) {
                Ty const* other = ty_apparent_type(me, ty->composite.types.data[q]);
                owners += (other->flags & TYF_OBJECT) && ty_get_prop(me, other, prop->name);
            }
            if (count == cap) {
                cap = cap ? cap * 2 : 8;
                *out = realloc(*out, sizeof(ObjectLiteralProp) * cap);
                panic_if(!*out, "out of memory");
            }
            if (owners == 1) {
                (*out)[count++] = (ObjectLiteralProp){ .name = prop->name, .reuse = prop };
                continue;
            }
            FoundProp found = ty_find_prop(me, ty, prop->name);
            (*out)[count++] = (ObjectLiteralProp){
                .name = prop->name, .flags = found.flags, .type = found.type, .name_span = prop->decl->name_span,
                .name_form = ty_prop_name_form_of(prop),
            };
        }
    }
    return count;
}

// `{ default: T }`, with T's own properties too if `spread` (tsc's createDefaultPropertyWrapperForModule, and
// getSpreadType of the type with it in getTypeWithSyntheticDefaultImportType)
static Ty const* with_synthetic_default(Checker* me, Ty const* type, bool spread, ObjectLiteralKind kind) {
    Atom const* default_name = my atoms.default_;
    ObjectLiteralProp* from = nullptr;
    u32 from_count = 0;
    Members const* members = spread && (type->flags & (TYF_OBJECT | TYF_INTERSECTION)) ? ty_members(me, type) : nullptr;
    if (members && type->kind == TY_INTERSECTION) {
        from_count = intersection_props(me, type, &from);
    } else if (members) {
        from = malloc(sizeof(ObjectLiteralProp) * (members->prop_count + 1));
        panic_if(!from, "out of memory");
        for (u32 i = 0; i < members->prop_count; i++) {
            Prop const* prop = members->props[i];
            // (A readonly one is copied without readonly: tsc's getSpreadSymbol with readonly false. An exported const)
            if (!(prop->flags & PROP_READONLY)) {
                from[from_count++] = (ObjectLiteralProp){ .reuse = prop };
                continue;
            }
            // (tsc's spread symbol keeps the declarations: a property signature's annotation is reused)
            PropDecl const* decl = prop->decl;
            TypeNode const* annotation = !prop->target && decl->node && decl->node->parsed && !decl->lazy &&
                !decl->function && !prop_decl_rare(decl)->overload_count && !decl->symbol ? decl->node : nullptr;
            from[from_count++] = (ObjectLiteralProp){
                .name = prop->name, .flags = (prop->flags & (PROP_OPTIONAL | PROP_METHOD)) | PROP_NO_VALUE_DECLARATION, .type = prop_type(me, prop),
                .name_span = prop->decl->name_span, .name_form = ty_prop_name_form_of(prop),
                .decl_spans = prop->decl->decl_spans, .decl_span_count = prop->decl->decl_span_count,
                .spread_variable = prop->decl->symbol, .spread_fresh = true, .asserted = annotation,
            };
        }
    }
    ObjectLiteralProp* props = arena_new_n(&my arena, ObjectLiteralProp, from_count + 1);
    props[0] = (ObjectLiteralProp){ .name = default_name, .type = type };
    u32 n = 1;
    for (u32 i = 0; i < from_count; i++) {
        Atom const* name = from[i].reuse ? from[i].reuse->name : from[i].name;
        if (name != default_name) props[n++] = from[i];
    }
    free(from);
    return ty_object_literal(me, props, n, members ? members->index : nullptr, members ? members->index_count : 0,
        OBJECT_SYNTHETIC | kind);
}

// tsc's cloneTypeAsModuleType of a type that gets no synthetic default: its members as they are, in a type of its
// own (printed structurally, not by the symbol it was)
static Ty const* module_type_clone(Checker* me, Ty const* type, ObjectLiteralKind kind) {
    if (type->kind != TY_ANONYMOUS && type->kind != TY_DECLARED && type->kind != TY_REFERENCE) return nullptr;
    Members const* members = ty_members(me, type);
    if (members->call_count || members->construct_count) return nullptr;
    ObjectLiteralProp* props = arena_new_n(&my arena, ObjectLiteralProp, members->prop_count + 1);
    for (u32 i = 0; i < members->prop_count; i++) props[i] = (ObjectLiteralProp){ .reuse = members->props[i] };
    return ty_object_literal(me, props, members->prop_count, members->index, members->index_count, OBJECT_SYNTHETIC | kind);
}

// tsc's getTypeWithSyntheticDefaultImportType caches its answer on the module type (type.syntheticType), decided by the
// first import that asks with its own usage: a static `import * as m` of a CommonJS module in a CommonJS file says no,
// and a later `await import()` of it, which would say yes on its own, gets the module without a synthetic default
// (bun's 08-C7a/b). Per importing file here, not per checker like tsc's: which files share a checker (and so which
// import asks first) depends on the checker count, tsgo's assignment being another one (opencode/packages/core's
// output differed between 1 and 4 checkers with a per-checker cache)
static bool synthetic_default_first(Checker* me, Ty const* type, SourceFile const* importer, bool decision) {
    static char const yes = 1, no = 0;
    u64 key[3] = { KEY_SYNTHETIC_DEFAULT, (u64)(uintptr_t)type, (u64)(uintptr_t)importer };
    void const* known = intern_get(&my links, key, 3);
    if (known) return known == &yes;
    intern_set(me, &my links, intern_slot(me, &my links, key, 3), decision ? &yes : &no);
    return decision;
}

static bool dynamic_import_has_synthetic_default(Checker* me, Symbol* module, SourceFile const* importer);

// tsc's resolveExportByName: of a module with `export =`, a property of what it assigns
static bool module_has_export(Checker* me, Symbol* module, csview name);

// tsc's canHaveSyntheticDefault for a static import in `importer`: an ES module's import goes like an import call's,
// a CommonJS file's doesn't have Node.js's CommonJS-into-ESM rule (getEmitSyntaxForUsageLocation: CommonJS)
static bool static_import_has_synthetic_default(Checker* me, Symbol* module, SourceFile const* importer) {
    ModuleKind usage = importer ? source_file_emit_module_kind(importer) : MODULE_KIND_NONE;
    bool esm_syntax = usage == MODULE_KIND_PRESERVE || (usage >= MODULE_KIND_ES2015 && usage <= MODULE_KIND_ESNEXT);
    if (esm_syntax) return dynamic_import_has_synthetic_default(me, module, importer);
    SourceFile const* target = module->module ? module->module->file : nullptr;
    if (!target || target->is_declaration) {
        return !symbol_get_export(module, c_sv("default")) && !module_has_export(me, module, c_sv("__esModule"));
    }
    if (!target->is_js) return module_export_equals(module) != module;
    return !span_valid(target->module_indicator) && !module_has_export(me, module, c_sv("__esModule"));
}

// `import * as ns` of a module with `export =` (tsc's resolveESModuleSymbol): what it assigns, with a synthetic
// `default` under esModuleInterop when that is something to import by default; an ES module's import of a JSON
// module has nothing but the default (isOnlyImportableAsDefault). Null if it is the assigned value as it is
static Ty const* namespace_import_type_uncached(Checker* me, Symbol const* symbol);
static Ty const* namespace_import_type(Checker* me, Symbol const* symbol) {
    if (!symbol || !(symbol->flags & SYMBOL_ALIAS) || !symbol->alias || !symbol->alias->item) return nullptr;
    PortItem const* item = symbol->alias->item;
    if (item->tag != PORT_IMPORT_GATHER || item->is_require || !item->module_ref) return nullptr;
    // (Made once per alias: tsc's resolveAlias caches the clone in the alias's links)
    u64 key[2] = { KEY_NAMESPACE_IMPORT, (u64)(uintptr_t)symbol };
    void const* cached = intern_get(&my links, key, 2);
    if (cached) return cached == (void const*)symbol ? nullptr : cached;
    Ty const* type = namespace_import_type_uncached(me, symbol);
    if (type) {
        u64 clone_key[2] = { KEY_MODULE_CLONE, (u64)(uintptr_t)type };
        intern_set(me, &my links, intern_slot(me, &my links, clone_key, 2), symbol);
    }
    void const* there = intern_set(me, &my links, intern_slot(me, &my links, key, 2), type ? (void const*)type : (void const*)symbol);
    return there == (void const*)symbol ? nullptr : there;
}

static Ty const* namespace_import_type_uncached(Checker* me, Symbol const* symbol) {
    PortItem const* item = symbol->alias->item;
    Symbol* module = binder_module_of_ref(symbol->binder, item->module_ref);
    Symbol* assigned = module ? module_export_equals(module) : nullptr;
    if (!assigned || assigned == module || assigned->is_placeholder) return nullptr;
    CompilerOptions const* options = compiler_options();
    SourceFile const* importer = span_source(item->decl_name);
    SourceFile const* target = item->module_ref->file;
    bool node = options->module >= MODULE_KIND_NODE16 && options->module <= MODULE_KIND_NODENEXT;
    bool esm_usage = node && importer && importer->is_esm;
    Ty const* type = ty_type_of_symbol(me, assigned);
    if (type->flags & TYF_ERROR) return nullptr;
    if (esm_usage && target && target->is_json) return with_synthetic_default(me, type, false, OBJECT_MODULE_CLONE);
    // (tsc's isESMFormatImportImportingCommonjsFormatFile: the import's emit syntax and the target's format for emit,
    // with any module setting)
    ModuleKind usage = importer ? source_file_emit_module_kind(importer) : MODULE_KIND_NONE;
    bool esm_syntax = usage == MODULE_KIND_PRESERVE || (usage >= MODULE_KIND_ES2015 && usage <= MODULE_KIND_ESNEXT);
    bool esm_cjs = esm_syntax && target && source_file_implied_format_for_emit(target) == MODULE_KIND_COMMONJS;
    Ty const* apparent = ty_apparent_type(me, type);
    // (tsc's getSignaturesOfStructuredType: an intersection has the signatures of its members)
    Members const* members = (apparent->flags & (TYF_OBJECT | TYF_INTERSECTION)) ? ty_members(me, apparent) : nullptr;
    bool callable = members && (members->call_count || members->construct_count);
    bool has_default = members && ty_get_prop(me, apparent, my atoms.default_);
    // (A union's too, a property of every member: tsc's getPropertyOfType)
    if (apparent->kind == TY_UNION && !(apparent->flags & TYF_BOOLEAN)) {
        has_default = true;
        for (u32 i = 0; i < apparent->composite.types.len && has_default; i++) {
            Ty const* part = ty_apparent_type(me, apparent->composite.types.data[i]);
            has_default = (part->flags & (TYF_OBJECT | TYF_INTERSECTION)) && ty_get_prop(me, part, my atoms.default_);
        }
    }
    if (!callable && !has_default && !esm_cjs) return nullptr;
    // A module that says it's an ES module has no synthetic default (tsc's canHaveSyntheticDefault), nor does a
    // declaration file whose `export =` value has a `default` that a module declares (`export default`: resolveExportByName
    // finds the property, isSyntacticDefault)
    bool synthetic = !module_has_export(me, module, c_sv("__esModule"));
    if (synthetic && has_default && (!target || target->is_declaration)) {
        Prop const* found = ty_get_prop(me, apparent, my atoms.default_);
        while (found && found->target) found = found->target;
        Symbol const* declared = found && found->decl ? found->decl->symbol : nullptr;
        // (A module's type has its exports resolved: the `default` export of the module that declares it says)
        SourceFile const* declaring = declared && declared->decl_count ? span_source(declared->decls[0].full) : nullptr;
        Symbol* declaring_module = declaring && declaring->scope ? declaring->scope->container : nullptr;
        Symbol const* exported = declaring_module && declaring_module->is_module ? symbol_get_export(declaring_module, c_sv("default")) : nullptr;
        bool syntactic = declared && (declared->is_default_export || (declared->flags & SYMBOL_ALIAS));
        if (!syntactic && exported && (exported->is_default_export || (exported->flags & SYMBOL_ALIAS))) {
            syntactic = symbol_resolve_alias((Symbol*)exported) == declared || exported == declared;
        }
        if (syntactic) {
            Ty const* clone = module_type_clone(me, type, OBJECT_MODULE_CLONE);
            if (clone) return clone;
            synthetic = false;
        }
    }
    bool structured = type->flags & (TYF_OBJECT | TYF_UNION | TYF_INTERSECTION | TYF_TYPE_PARAM | TYF_DEFERRED);
    if (!structured) return with_synthetic_default(me, type, false, OBJECT_MODULE_CLONE);
    synthetic = synthetic_default_first(me, type, importer, synthetic);
    // A union's clone has no properties: tsc's cloneTypeAsModuleType takes resolveStructuredTypeMembers' members, which a
    // union (also as the spread with its synthetic default, getSpreadType mapping over it) doesn't have: `{}`
    if (type->kind == TY_UNION && !(type->flags & TYF_BOOLEAN)) {
        return ty_object_literal(me, nullptr, 0, nullptr, 0, OBJECT_SYNTHETIC | OBJECT_MODULE_CLONE);
    }
    return synthetic ? with_synthetic_default(me, type, true, OBJECT_MODULE_CLONE) : nullptr;
}

// tsc's checkImportBinding resolves a namespace import's alias when it checks the declaration
// (resolveESModuleSymbol), which is where a module type's synthetic default is first decided (synthetic_default_first())
void ty_check_namespace_import(Checker* me, Symbol const* alias) {
    if (!alias || !(alias->flags & SYMBOL_ALIAS) || !alias->alias || !alias->alias->item) return;
    PortItem const* item = alias->alias->item;
    if (item->tag != PORT_IMPORT_GATHER || item->is_require || !item->module_ref) return;
    Symbol* module = binder_module_of_ref(alias->binder, item->module_ref);
    if (!module) return;
    if (module_export_equals(module) != module) {
        namespace_import_type(me, alias);
        return;
    }
    // A module without `export =`: its type asks when it has a `default` (or the import is ESM-of-CJS)
    SourceFile const* importer = span_source(item->decl_name);
    SourceFile const* target = item->module_ref->file;
    ModuleKind usage = importer ? source_file_emit_module_kind(importer) : MODULE_KIND_NONE;
    bool esm_syntax = usage == MODULE_KIND_PRESERVE || (usage >= MODULE_KIND_ES2015 && usage <= MODULE_KIND_ESNEXT);
    bool esm_cjs = esm_syntax && target && source_file_implied_format_for_emit(target) == MODULE_KIND_COMMONJS;
    if (!esm_cjs && !symbol_get_export(module, c_sv("default"))) return;
    synthetic_default_first(me, ty_type_of_symbol(me, module), importer, static_import_has_synthetic_default(me, module, importer));
}

Ty const* ty_namespace_import_original(Checker* me, Symbol const* alias) {
    if (!namespace_import_type(me, alias)) return nullptr;
    return ty_type_of_symbol(me, module_export_equals(binder_module_of_ref(alias->binder, alias->alias->item->module_ref)));
}

bool ty_is_constructor_declared(Symbol const* symbol) {
    bool in_constructor = false;
    for (u32 i = 0; i < symbol->decl_count; i++) {
        SymbolDecl const* decl = &symbol->decls[i];
        if (decl->kind != SYMBOL_DECL_ASSIGNMENT || symbol_decl_jsdoc_type(decl)) return false;
        // (`/** @type {T} */ this["x"];` declares without assigning)
        Expr const* node = decl->node;
        if (node && node->tag == EXPR_PROPERTY && span_eq(expr_outer_span(node), decl->full)) return false;
        if (decl->assignment != ASSIGNMENT_DECL_THIS_IN_CONSTRUCTOR && decl->assignment != ASSIGNMENT_DECL_THIS_IN_METHOD) {
            return false;
        }
        in_constructor |= decl->assignment == ASSIGNMENT_DECL_THIS_IN_CONSTRUCTOR;
    }
    return in_constructor;
}

// tsc's getTypeOfPropertyInBaseClass: the type of a class member's namesake in the base class, null if none
static Ty const* base_class_property_type(Checker* me, Symbol const* member) {
    ClassDecl const* class_decl = member->parent ? symbol_find_node(member->parent, SYMBOL_DECL_CLASS) : nullptr;
    Ty const* base = class_decl ? ty_class_base_instance(me, class_decl) : nullptr;
    if (!base) return nullptr;
    // (Its getTypeOfSymbol: an optional one with its undefined, or exactOptionalPropertyTypes' missing type)
    FoundProp found = ty_find_prop(me, ty_apparent_type(me, base), checker_atom(me, member->name));
    if (!found.found) return nullptr;
    return found.type;
}

// What `this.x` is at `flow` in the class constructor that declares x by assignment (tsc's getFlowTypeOfProperty: it
// starts out as the base class's x, else undefined, auto-typed from there)
Ty const* ty_constructor_declared_flow_type(Checker* me, Symbol const* member, Expr const* reference, FlowNode const* flow) {
    Ty const* base = base_class_property_type(me, member);
    Ty const* ret = ty_flow_type(me, reference, ty_intrinsic(me, TY_AUTO), base ? base : ty_intrinsic(me, TY_UNDEFINED), flow);
    return ret == ty_intrinsic(me, TY_AUTO) ? ty_intrinsic(me, TY_ANY) : ret;
}

// A member only `this.x = v` assignments declare, one of them in a class constructor: the type `this.x` has at the end of
// the constructor (tsc's getFlowTypeInConstructor). Null if that is only null or undefined, or it isn't such a member
static Ty const* flow_type_in_constructor(Checker* me, Symbol const* symbol) {
    if (!symbol_rare(symbol)->constructor_assigned || !symbol_rare(symbol)->declaring_constructor || !function_impl_return_flow(symbol_rare(symbol)->declaring_constructor)) {
        return nullptr;
    }
    Ty const* ret = ty_constructor_declared_flow_type(me, symbol, symbol_rare(symbol)->constructor_assigned, function_impl_return_flow(symbol_rare(symbol)->declaring_constructor));
    // Nothing but `[]` (tsc's autoArrayType, which never evolves here): any[], reported (getFlowTypeInConstructor)
    SourceFile const* file = span_source(symbol_rare(symbol)->constructor_assigned->inner_span);
    if (file && file->is_js && ret == ty_auto_array(me)) {
        TY_WORKER_STATE(me);
        *intern_slot(me, &my links, (u64[]){ KEY_JS_IMPLICIT_ANY, (u64)(uintptr_t)symbol }, 2) = (void const*)(uintptr_t)3;
        return ty_array(me, ty_intrinsic(me, TY_ANY), false);
    }
    TyList parts = ret->kind == TY_UNION ? ret->composite.types : (TyList){ .data = &ret, .len = 1 };
    for (u32 i = 0; i < parts.len; i++) {
        if (!(parts.data[i]->flags & TYF_NULLABLE)) return ret;
    }
    return nullptr;
}

typedef struct SameThisProperty { csview name; bool found; } SameThisProperty;

static AstWalk visit_same_this_property(Expr const* expr, void* arg) {
    SameThisProperty* state = arg;
    if (expr->tag == EXPR_FUNCTION) return AST_WALK_SKIP;
    if (expr->tag == EXPR_PROPERTY && expr_this_keyword(&expr->property->expr) &&
        csview_equals_sv(name_cooked(span_sv(expr_static_property_name(expr))), state->name)
    ) {
        state->found = true;
        return AST_WALK_STOP;
    }
    return AST_WALK_CONTINUE;
}

// Whether `value` reads `this.<name>` outside of nested functions (tsgo's containsSameNamedThisProperty)
static bool reads_same_this_property(Expr const* value, csview name) {
    SameThisProperty state = { .name = name };
    ast_walk_expr(value, visit_same_this_property, &state);
    return state.found;
}

// tsc's getWidenedTypeForAssignmentDeclaration: the union of what the assignments assign. A member `this.x = v`
// declares in a class constructor has the type the constructor gives it (flow_type_in_constructor()), else (a
// constructor function's) the union of the constructor's assignments. TODO: a JSDoc type on one of them is the type
static Ty const* assignment_symbol_type(Checker* me, Symbol const* symbol) {
    if (is_js_container(symbol, symbol->decls[0].node)) return ty_namespace_type(me, symbol);
    // An export every assignment of which is an alias (`exports.x = undefined; exports.x = f`) is what the last one
    // names (tsc's exportAssignmentIsAlias declares an Alias, which getDeclarationOfAliasSymbol resolves by its last
    // declaration)
    // (tsc: autoType for the export itself, which its own file's accesses type by control flow, see
    // duplicated_commonjs_export(); an importer gets the flow type at the end of the file, getFlowTypeFromCommonJSExport,
    // which for top-level assignments is the last one's)
    if (ty_is_duplicated_commonjs_alias_export(symbol)) {
        SymbolDecl const* last = &symbol->decls[symbol->decl_count - 1];
        if (last->kind != SYMBOL_DECL_ALIAS) return value_entry_type(me, symbol, last);
        Symbol const* target = symbol_resolve_alias((Symbol*)symbol);
        if (target && target != symbol) return ty_type_of_symbol(me, target);
    }
    // The first JSDoc `@type` of an assignment is the type, as declared (tsgo has no definedInMethod `| undefined`);
    // the last one when every declaration is a `this.x =` (tsgo's isConstructorDeclaredThisProperty overwrites it)
    bool all_this = symbol->decl_count > 0;
    for (u32 i = 0; i < symbol->decl_count && all_this; i++) {
        AssignmentDeclKind kind = symbol->decls[i].assignment;
        all_this = symbol->decls[i].kind == SYMBOL_DECL_ASSIGNMENT &&
            (kind == ASSIGNMENT_DECL_THIS_IN_CONSTRUCTOR || kind == ASSIGNMENT_DECL_THIS_IN_METHOD);
    }
    TypeNode const* annotation = nullptr;
    for (u32 i = 0; i < symbol->decl_count && (all_this || !annotation); i++) {
        if (symbol->decls[i].kind == SYMBOL_DECL_ASSIGNMENT && symbol_decl_jsdoc_type(&symbol->decls[i])) {
            annotation = symbol_decl_jsdoc_type(&symbol->decls[i]);
        }
    }
    if (annotation) return ty_of_node(annotation);
    Ty const* stack[16];
    Ty const** types = symbol->decl_count <= c_arraylen(stack) ? stack : malloc(sizeof(Ty const*) * symbol->decl_count);
    // `this.x = v` declarations only (tsgo's isConstructorDeclaredThisProperty): one in the constructor gives the type
    // `this.x` has at its end (getFlowTypeInConstructor), unless that is only null or undefined; only methods' take
    // the base class's namesake (getTypeOfPropertyInBaseClass), else what they assign, possibly undefined
    bool in_constructor = false;
    for (u32 i = 0; i < symbol->decl_count; i++) in_constructor |= symbol->decls[i].assignment == ASSIGNMENT_DECL_THIS_IN_CONSTRUCTOR;
    Ty const* in_constructor_type = all_this && in_constructor && ty_is_constructor_declared(symbol) ? flow_type_in_constructor(me, symbol) : nullptr;
    if (in_constructor_type) {
        if (types != stack) free(types);
        // (Widened like any assignment declaration's type: tsc's getWidenedTypeForAssignmentDeclaration)
        return ty_widened_objects(me, ty_widened_literal(me, in_constructor_type));
    }
    bool in_method = all_this && !in_constructor;
    Ty const* base_type = in_method ? base_class_property_type(me, symbol) : nullptr;
    if (base_type) {
        if (types != stack) free(types);
        return ty_widened_nullable(me, base_type);
    }
    u32 count = 0;
    // (`module.exports = v`, `exports.x = v`: tsgo's getAssignmentDeclarationInitializerType keeps their literals)
    bool commonjs_export = csview_equals(symbol->name, "export=") || (symbol->parent && symbol->parent->is_module);
    for (u32 i = 0; i < symbol->decl_count; i++) {
        SymbolDecl const* decl = &symbol->decls[i];
        if (commonjs_export && decl->kind == SYMBOL_DECL_ALIAS && decl->js && decl->js->commonjs_value) {
            types[count++] = ty_regular(me, ty_expr_type(me, decl->js->commonjs_value));
            continue;
        }
        if (decl->kind != SYMBOL_DECL_ASSIGNMENT) continue;
        // (Not one that reads `this.x` itself: tsgo's containsSameNamedThisProperty)
        if ((decl->assignment == ASSIGNMENT_DECL_THIS_IN_CONSTRUCTOR || decl->assignment == ASSIGNMENT_DECL_THIS_IN_METHOD) &&
            decl->node && reads_same_this_property(decl->node, symbol->name)
        ) {
            continue;
        }
        Ty const* type = commonjs_export && decl->assignment != ASSIGNMENT_DECL_DEFINE_PROPERTY ?
            ty_regular(me, ty_expr_type(me, decl->node)) : assignment_declaration_type(me, symbol, decl);
        // (A CommonJS export's first assignment of undefined is ignored when there are others: tsgo's
        // getWidenedTypeForAssignmentDeclaration)
        if (commonjs_export && i == 0 && symbol->decl_count > 1 && (type->flags & TYF_UNDEFINED)) continue;
        types[count++] = type;
    }
    // Nothing to go by (every declaration reads the property itself, `this.n = this.n + 1`): any, and no implicit any
    // reported (tsgo's getWidenedTypeForAssignmentDeclaration: anyType, which isn't all nullable)
    if (!count) {
        if (types != stack) free(types);
        return ty_intrinsic(me, TY_ANY);
    }
    // (Object literals are widened together: the union has their properties as optional in each other. Reduced to
    // subtypes: tsc's getUnionType(types, UnionReduction.Subtype), a CommonJS export's literals only, like tsgo's)
    Ty const* ret = ty_widened_objects(me, ty_union(me, types, count, nullptr, (TyList){0}));
    if (types != stack) free(types);
    // Only methods assign it: possibly undefined
    if (in_method && count && compiler_options()->strict_null_checks) {
        Ty const* parts[2] = { ret, ty_intrinsic(me, TY_UNDEFINED) };
        ret = ty_union(me, parts, 2, nullptr, (TyList){0});
    }
    // Nothing but null and undefined is any in JavaScript
    SourceFile const* file = span_source(symbol->decls[0].full);
    if (file && file->is_js && ty_only_nullable(ty_widened_nullable(me, ret))) {
        TY_WORKER_STATE(me);
        *intern_slot(me, &my links, (u64[]){ KEY_JS_IMPLICIT_ANY, (u64)(uintptr_t)symbol }, 2) = (void const*)(uintptr_t)2;
        return ty_intrinsic(me, TY_ANY);
    }
    return ty_widened_nullable(me, ret);
}

// Whether the type of a JavaScript assignment declaration's member became any implicitly (tsc's reportImplicitAny in
// getWidenedTypeForAssignmentDeclaration): 1 if `value` (an assignment of `[]`) made it any[], 2 if only null or
// undefined are assigned (any), 3 if the constructor only assigns `[]` (any[]), else 0. Evaluates the member's type
int ty_js_assignment_implicit_any(Checker* me, Symbol const* member, Expr const* value) {
    // A class member only `this.x = v` outside of the constructor declares is the base class's namesake if it has one
    // (tsc's addInheritedMembers): the member's own type is never asked for, nor reported
    if (member->decl_count && member->decls[0].kind == SYMBOL_DECL_ASSIGNMENT &&
        member->parent && (member->parent->flags & SYMBOL_CLASS) && symbol_rare(member->parent)->members &&
        symbol_table_get(symbol_rare(member->parent)->members, member->name) == member && !ty_is_constructor_declared(member) &&
        base_class_property_type(me, member)
    ) {
        return 0;
    }
    // `o[k] = v` whose key isn't a literal or unique symbol type declares nothing (tsc's isLateBindableName): no member
    // to type nor report
    Expr const* late_bound_key = symbol_rare(member)->late_bound_key;
    if (late_bound_key) {
        bool is_number = false;
        if (!ty_property_name_of_key(me, ty_expr_type(me, late_bound_key), &is_number)) return 0;
    }
    ty_type_of_symbol(me, member);
    if (intern_get(&my links, (u64[]){ KEY_JS_IMPLICIT_ANY, (u64)(uintptr_t)value }, 2)) return 1;
    return (int)(uintptr_t)intern_get(&my links, (u64[]){ KEY_JS_IMPLICIT_ANY, (u64)(uintptr_t)member }, 2);
}

// tsc's canHaveSyntheticDefault for `import()` from `importer` (whose usage mode is ESM under node16-nodenext and
// preserve, else the importer's module format's)
static bool module_has_export(Checker* me, Symbol* module, csview name) {
    Symbol* assigned = module_export_equals(module);
    if (assigned == module || !assigned || assigned->is_placeholder) return symbol_get_export(module, name) != nullptr;
    Ty const* type = ty_apparent_type(me, ty_type_of_symbol(me, assigned));
    return (type->flags & (TYF_OBJECT | TYF_INTERSECTION)) && ty_get_prop(me, type, checker_atom(me, name));
}

static bool dynamic_import_has_synthetic_default(Checker* me, Symbol* module, SourceFile const* importer) {
    CompilerOptions const* options = compiler_options();
    SourceFile const* target = module->module ? module->module->file : nullptr;
    bool node = options->module >= MODULE_KIND_NODE16 && options->module <= MODULE_KIND_NODENEXT;
    bool esm_usage = node || options->module == MODULE_KIND_PRESERVE ||
        (importer && importer->has_format ? importer->is_esm : options->module >= MODULE_KIND_ES2015);
    // The target's format for emit (tsc's getImpliedNodeFormatForEmit): outside node16-nodenext only what its
    // extension or package.json `type` says
    csview path = target ? cstr_sv(&target->canonical_path) : c_sv("");
    bool explicit_format = target && target->has_format && (node || target->package_type_decides ||
        csview_ends_with(path, ".mts") || csview_ends_with(path, ".mjs") || csview_ends_with(path, ".cts") ||
        csview_ends_with(path, ".cjs"));
    if (explicit_format && esm_usage) {
        // In Node.js, a CommonJS module always has a synthetic default when imported into an ES module, and two ES
        // modules have none, whatever the module setting
        if (!target->is_esm && node) return true;
        if (target->is_esm) return false;
    }
    if (!target || target->is_declaration) {
        // (Not with a default export of its own, or an `__esModule` marker)
        return !symbol_get_export(module, c_sv("default")) && !module_has_export(me, module, c_sv("__esModule"));
    }
    // A TypeScript file only with `export =`; a JavaScript file without ES module syntax or an `__esModule` marker
    if (!target->is_js) return module_export_equals(module) != module;
    return !span_valid(target->module_indicator) && !module_has_export(me, module, c_sv("__esModule"));
}

// What `import("m")` resolves to (tsc's checkImportCallExpression): the module (or what its `export =` assigns),
// with a synthetic `default` where it can have one (getTypeWithSyntheticDefaultImportType), or nothing but a default
// for a JSON module imported into an ES module under node16-nodenext (getTypeWithSyntheticDefaultOnly)
Ty const* ty_dynamic_import_module_type(Checker* me, Symbol* module, SourceFile const* importer) {
    Symbol* assigned = module_export_equals(module);
    Ty const* type = ty_type_of_symbol(me, assigned);
    if (assigned->is_placeholder || (type->flags & TYF_ERROR)) return type;
    CompilerOptions const* options = compiler_options();
    SourceFile const* target = module->module ? module->module->file : nullptr;
    bool node = options->module >= MODULE_KIND_NODE16 && options->module <= MODULE_KIND_NODENEXT;
    if (node && target && target->is_json) {
        return with_synthetic_default(me, type, false, OBJECT_EMPTY_SYMBOL);
    }
    if (!synthetic_default_first(me, type, importer, dynamic_import_has_synthetic_default(me, module, importer))) return type;
    // (tsc's isValidSpreadType: an object spreads, anything else is only the default; any spread is any)
    if (type->flags & TYF_ANY) return type;
    bool structured = type->flags & (TYF_OBJECT | TYF_UNION | TYF_INTERSECTION | TYF_TYPE_PARAM | TYF_DEFERRED);
    // (Its empty_symbol is set by its constructor: a type of its own, not a mark on a shared structural one)
    return with_synthetic_default(me, type, structured, OBJECT_EMPTY_SYMBOL);
}


// The type of a symbol as a value. Give it the symbol a name found or the one that has the value meaning (`target`
// of an identifier), aliases are seen through. Everything about a placeholder is any
// A symbol only assignments declare: what they assign, any if that reads the symbol again (tsgo's
// getWidenedTypeForAssignmentDeclaration pushes the symbol: TS7022 at its value declaration, the first assignment)
static Ty const* guarded_assignment_symbol_type(Checker* me, Symbol const* symbol) {
    SymbolDecl const* decl = &symbol->decls[0];
    bool single = symbol->decl_count == 1 && decl->assignment == ASSIGNMENT_DECL_PROPERTY && !symbol_decl_jsdoc_type(decl);
    // (A class's `this.x = v` members are typed by control flow in the constructor: not here)
    if (decl->assignment == ASSIGNMENT_DECL_THIS_IN_CONSTRUCTOR) {
        return single ? value_entry_type(me, symbol, decl) : assignment_symbol_type(me, symbol);
    }
    Ty const* circular = ty_intrinsic(me, TY_CIRCULAR);
    u64 key[2] = { KEY_CIRCULAR_BINDING, (u64)(uintptr_t)decl };
    if (my circular_bindings && intern_get(&my links, key, 2)) {
        TY_WORKER_STATE(me); // (A recorded cycle: this worker's)
        return circular;
    }
    if (ty_resolving_find(me, RESOLVING_DECLARATION, decl)) {
        ty_resolving_cycle(me, RESOLVING_DECLARATION, decl);
        return circular;
    }
    ty_resolving_push(me, RESOLVING_DECLARATION, decl);
    Ty const* type = single ? value_entry_type(me, symbol, decl) : assignment_symbol_type(me, symbol);
    if (ty_resolving_pop(me)) {
        my circular_bindings = true;
        *intern_slot(me, &my links, key, 2) = (void*)decl;
        if (compiler_options()->no_implicit_any && span_valid(decl->full)) {
            // (At the whole assignment)
            Expr const* value = decl->node;
            Span whole = decl->full;
            if (value && decl->kind == SYMBOL_DECL_ASSIGNMENT) {
                Span value_span = expr_outer_span(value);
                if (span_source(value_span) == span_source(whole) && span_end_ptr(value_span) > span_start_ptr(whole)) {
                    whole = span_make_in(whole, span_start_ptr(whole), span_end_ptr(value_span));
                }
            }
            ty_error_at(me, whole, 7022, cstr_d("'", symbol->name, "' implicitly has type 'any' because it does not "
                "have a type annotation and is referenced directly or indirectly in its own initializer."));
        }
        return circular;
    }
    // (Evaluated again from below a call's barrier and found circular there: that inner result is the one remembered,
    // tsc's getTypeOfVariableOrParameterOrProperty keeps the first resolvedType set)
    if (my circular_bindings && intern_get(&my links, key, 2)) {
        TY_WORKER_STATE(me); // (A recorded cycle: this worker's)
        return circular;
    }
    return type;
}

// An import of a module with `export =` (a name of it, its default, the namespace): tsc resolves it through the type of
// what `export =` assigns (getExternalModuleMember's getPropertyOfType, getTargetOfModuleDefault, resolveESModuleSymbol),
// so a cycle through that type is the alias's own (resolveAlias re-entered: TS2303). Null for any other symbol
static PortItem const* export_equals_import(Symbol const* symbol, Symbol** export_equals) {
    PortItem const* item = symbol->alias ? symbol->alias->item : nullptr;
    if (!item || item->jsdoc || !item->module_ref ||
        (item->tag != PORT_IMPORT_DEFAULT && item->tag != PORT_IMPORT_GATHER && item->tag != PORT_IMPORT_SPECIFIC) ||
        (item->tag == PORT_IMPORT_GATHER && item->is_require)
    ) {
        return nullptr;
    }
    Symbol* module = binder_module_of_ref(symbol->binder, item->module_ref);
    Symbol* found = module ? symbol_get_export(module, c_sv("export=")) : nullptr;
    if (!found) return nullptr;
    *export_equals = found;
    return item;
}

static Ty const* alias_type_of_symbol(Checker* me, Symbol const* symbol);

Ty const* ty_type_of_symbol(Checker* me, Symbol const* symbol) {
    Symbol* export_equals = nullptr;
    PortItem const* item = symbol && (symbol->flags & SYMBOL_ALIAS) ? export_equals_import(symbol, &export_equals) : nullptr;
    if (!item) return alias_type_of_symbol(me, symbol);
    // (Once circular its type is the error type for good: tsc's getTypeOfAlias keeps the resolvedType it failed with)
    if (intern_get(&my links, (u64[]){ KEY_ALIAS_TYPE_CIRCULAR, (u64)(uintptr_t)symbol }, 2)) {
        TY_WORKER_STATE(me);
        return ty_intrinsic(me, TY_ERROR);
    }
    ty_resolving_push(me, RESOLVING_DECLARATION, symbol);
    Ty const* ret = alias_type_of_symbol(me, symbol);
    if (ty_resolving_pop(me)) {
        TY_WORKER_STATE(me);
        *intern_slot(me, &my links, (u64[]){ KEY_ALIAS_TYPE_CIRCULAR, (u64)(uintptr_t)symbol }, 2) = symbol;
        ty_error_at(me, item->decl_name, 2303, cstr_d("Circular definition of import alias '", item->decl_name, "'."));
        // (A default import of the file's own module goes through its `export =` alias's own type, getTypeOfAlias,
        // which is on the cycle too: TS7022 at it, named by what it assigns)
        SourceFile const* importer = span_source(item->decl_name);
        bool self = importer && importer->scope && importer->scope->container &&
            binder_module_of_ref(symbol->binder, item->module_ref) == importer->scope->container;
        if (self && item->tag == PORT_IMPORT_DEFAULT && compiler_options()->no_implicit_any && export_equals->decl_count &&
            span_valid(export_equals->decls[0].full) && (export_equals->flags & SYMBOL_ALIAS) && export_equals->alias &&
            span_valid(export_equals->alias->name)
        ) {
            ty_error_at(me, span_with_semicolon(export_equals->decls[0].full), 7022, cstr_d("'", export_equals->alias->name,
                "' implicitly has type 'any' because it does not have a type annotation and is referenced directly or "
                "indirectly in its own initializer."));
        }
        ret = ty_intrinsic(me, TY_ERROR);
    }
    return ret;
}

static Ty const* alias_type_of_symbol(Checker* me, Symbol const* symbol) {
    if (symbol && (symbol->flags & SYMBOL_ALIAS)) {
        Ty const* namespace_import = namespace_import_type(me, symbol);
        if (namespace_import) return namespace_import;
        // (`/** @type {T} */ export default x` is still x's type: tsgo's getTypeOfAlias has no declared type)
        Symbol const* step = symbol;
        for (int depth = 0; step && (step->flags & SYMBOL_ALIAS) && depth < 32; depth++) {
            // A namespace import along the way is the module type it makes (tsc's getTargetOfNamespaceImport resolves
            // to resolveESModuleSymbol's clone): `export = Spec` of `import * as Spec` with a synthetic default
            if (depth) {
                Ty const* namespace_step = namespace_import_type(me, step);
                if (namespace_step) return namespace_step;
            }
            Symbol const* target = symbol_resolve_alias((Symbol*)step);
            // (An import's own step is the export it names, which may be an alias itself)
            PortItem const* item = step->alias ? step->alias->item : nullptr;
            Symbol const* next = item && item->source_symbol ? item->source_symbol : target;
            // (`export = Spec` names Spec, which may be a namespace import)
            if (!item && step->alias && !step->alias->entity && !step->alias->class_expr && step->alias->scope &&
                span_valid(step->alias->name)
            ) {
                Symbol* named = scope_resolve(step->alias->scope, span_sv(step->alias->name), SYMBOL_ANY_MEANING);
                if (named && named != step && (named->flags & SYMBOL_ALIAS)) next = named;
            }
            step = next;
            // (A default import that is what `export =` assigns: the `export =` alias, which may name a namespace import)
            // (So is `export as namespace N` of a module with `export =`: N is the module, what it assigns)
            bool as_namespace = item && item->tag == PORT_EXPORT_AS_NAMESPACE && span_source(item->decl_name) && span_source(item->decl_name)->scope;
            if (item && !item->source_symbol && ((item->tag == PORT_IMPORT_DEFAULT && item->module_ref) || as_namespace) && target) {
                Symbol* module = as_namespace ? span_source(item->decl_name)->scope->container : binder_module_of_ref(symbol->binder, item->module_ref);
                Symbol* export_equals = module ? symbol_get_export(module, c_sv("export=")) : nullptr;
                if (export_equals && (export_equals->flags & SYMBOL_ALIAS) && symbol_resolve_alias(export_equals) == target) {
                    step = export_equals;
                }
            }
        }
    }
    symbol = symbol_for_meaning(symbol, SYMBOL_VALUE);
    return own_type_of_symbol(me, symbol);
}

// ty_type_of_symbol() of a symbol that has the value meaning, without what a module's other exports add to what its
// `export =` assigns
static Ty const* own_type_of_symbol(Checker* me, Symbol const* symbol) {
    symbol = canonical(me, symbol);
    // The clone an augmented `export =` namespace import is (SymbolRare.export_equals_clone): that import's type
    if (symbol && symbol_rare(symbol)->clone_of_import) {
        Ty const* clone = namespace_import_type(me, symbol_rare(symbol)->clone_of_import);
        if (clone) return clone;
    }
    // (`export { undefined }` names tsc's undefinedSymbol, which tttt has no symbol for)
    if (symbol && symbol->is_placeholder && symbol->is_unresolved && csview_equals(symbol->name, "undefined")) {
        return ty_intrinsic(me, TY_UNDEFINED);
    }
    if (!symbol || symbol->is_placeholder) return ty_intrinsic(me, symbol && symbol->is_unresolved ? TY_ERROR : TY_ANY);
    // `typeof import("./a")`, `import("./a")`, `globalThis`. A shorthand ambient module is any
    if (symbol->is_module && symbol->is_shorthand_module) return ty_intrinsic(me, TY_ANY);
    if (symbol->is_module || symbol->is_global_this) {
        // (A function merged into a module: merge_into_umd_globals())
        if (symbol->is_module && symbol->flags & SYMBOL_FUNCTION) return ty_merged_value_type(me, symbol);
        return ty_namespace_type(me, symbol);
    }
    bool assignments = symbol->decl_count && symbol->decls[0].kind == SYMBOL_DECL_ASSIGNMENT;
    // (A declaration that isn't an assignment is the value declaration, whatever came first: tsc's setValueDeclaration,
    // `f.x = 1` in one file and `namespace f { export var x }` in another is the variable)
    for (u32 i = 1; assignments && i < symbol->decl_count; i++) {
        SymbolDeclKind kind = symbol->decls[i].kind;
        assignments = kind == SYMBOL_DECL_ASSIGNMENT || kind == SYMBOL_DECL_EXPANDO || !(symbol->decls[i].flags & SYMBOL_VALUE);
    }
    if (assignments) return guarded_assignment_symbol_type(me, symbol);
    SymbolDecl const* only = nullptr;
    for (u32 i = 0; i < symbol->decl_count; i++) {
        if (!decl_contributes_value(symbol, i)) continue;
        if (only) return ty_merged_value_type(me, symbol);
        only = &symbol->decls[i];
    }
    // Not memoized here: what gives a declaration its type has a memo of its own, which is also what detects cycles
    return only ? value_entry_type(me, symbol, only) : ty_intrinsic(me, TY_ERROR);
}

bool ty_is_non_local_function(Symbol const* symbol) {
    if (!(symbol->flags & SYMBOL_FUNCTION)) return false;
    if (symbol->parent) return true;
    for (u32 i = 0; i < symbol->decl_count; i++) {
        Scope const* block = symbol->decls[i].block;
        if (block && (block->kind == SCOPE_SCRIPT || block->kind == SCOPE_MODULE || block->kind == SCOPE_NAMESPACE ||
            block->kind == SCOPE_GLOBAL_AUGMENTATION)) {
            return true;
        }
    }
    return false;
}

// A function declared within a function (or a class, a block...) whose only other value declarations are expandos
// (`function f() {}; f.x = 1`): tsc's type of it is an anonymous object type (getTypeOfFuncClassEnumModule) whose
// outer type parameters are those in scope, instantiated with them: `function g<P>() { function f(p: P) {}; f.x = 1;
// return f }` is `{ (p: string): void; x: number }` for g<string>(). A file's or namespace's has none to instantiate,
// and stays `typeof f` by name
static bool is_local_expando_function(Symbol const* symbol) {
    if (!(symbol->flags & SYMBOL_FUNCTION) || (symbol->flags & SYMBOL_VALUE_MODULE) || symbol->is_module) return false;
    bool expando = false;
    for (u32 i = 0; i < symbol->decl_count; i++) {
        SymbolDeclKind kind = symbol->decls[i].kind;
        if (kind == SYMBOL_DECL_EXPANDO) expando = true;
        else if (kind != SYMBOL_DECL_FUNCTION && (symbol->decls[i].flags & SYMBOL_VALUE)) return false;
    }
    return expando && !ty_is_non_local_function(symbol);
}

// The value of a symbol that several declarations contribute a value to: function overloads, a function, class
// or enum merged with a namespace. The members of all of them (members.c: builder_add_merged_value)
Ty const* ty_merged_value_type(Checker* me, Symbol const* symbol) {
    u64 key[2] = { KEY_MERGED_VALUE, (u64)(uintptr_t)symbol };
    Ty const* existing = intern_get(&my links, key, 2);
    if (existing) return existing;

    // With a namespace, class or enum among them it goes by name (`typeof f`), overloads alone are anonymous
    Ty const* named = nullptr;
    Ty const** entries;
    u32 count = value_entries(me, symbol, &entries);
    for (u32 i = 0; i < count && !named; i++) {
        if (entries[i]->kind == TY_DECLARED) named = entries[i];
    }
    free(entries);
    if (named && is_local_expando_function(symbol)) named = nullptr;
    Ty* ret;
    if (named) {
        ret = named->declared.kind == TY_DECLARED_NAMESPACE ?
            make_namespace(me, named->declared.namespace_symbol) :
            ty_alloc(me, TY_DECLARED, TYF_OBJECT);
        if (named->declared.kind != TY_DECLARED_NAMESPACE) {
            ret->declared = named->declared;
            ret->declared.state = arena_new(&my arena, TyObjectState);
        }
        ret->declared.merged_value = symbol;
        if (!ret->declared.name->len) ret->declared.name = ty_qualified_name(me, symbol);
    } else {
        // A function body can mention every type parameter in scope
        ret = ty_alloc(me, TY_ANONYMOUS, TYF_OBJECT | TYF_COULD_CONTAIN_TYPE_VARIABLES);
        ret->anonymous.merged_value = symbol;
        ret->anonymous.ctx = (LowerCtx){ .generic = true };
        ret->anonymous.free_params_unknown = true;
        ret->anonymous.state = arena_new(&my arena, TyObjectState);
    }
    return intern_set(me, &my links, intern_slot(me, &my links, key, 2), ret);
}

u32 ty_merged_value_entries(Checker* me, Symbol const* symbol, Ty const*** out) {
    return value_entries(me, symbol, out);
}

static Ty const* tuple_target_of_node(Checker* me, TypeNode const* node) {
    u32 arity = (u32)vec_tuple_element_size(&node->tuple.elements);
    if (!arity) {
        TupleShape shape = { .readonly = node->tuple.readonly };
        return ty_tuple_target(me, &shape);
    }
    unsigned char* flags = malloc(arity);
    Atom const** names = calloc(arity, sizeof(Atom const*));
    void const** labels = calloc(arity, sizeof(void const*));
    bool any_named = false;
    for_each (i, vec_tuple_element, &node->tuple.elements) {
        // (`...a?: T[]`, TS5085: the question mark wins, tsc's getTupleElementFlags)
        flags[i_index] = i->optional ? TUPLE_ELEMENT_OPTIONAL : i->rest ? TUPLE_ELEMENT_REST : 0;
        if (span_valid(i->maybe_name)) {
            names[i_index] = checker_atom(me, span_sv(i->maybe_name));
            labels[i_index] = i;
            any_named = true;
        }
    }
    TupleShape shape = {
        .arity = arity,
        .readonly = node->tuple.readonly,
        .flags = flags,
        .names = any_named ? names : nullptr,
        .labels = any_named ? labels : nullptr,
    };
    Ty const* ret = ty_tuple_target(me, &shape);
    free(flags);
    free(names);
    free(labels);
    return ret;
}

static TypeNode const* array_element_type_node(TypeNode const* node);

// `[b?: number]` has an element of type `number | undefined` (tsc's addOptionality)

static Ty const* tuple_element_type(Checker* me, TupleElement const* element, LowerCtx ctx) {
    Ty const* type = ty_from_node(me, &element->type, ctx);
    // `...a?: T[]` (TS5085) is an optional element of T: the question mark decides the flag, the dots still take the
    // array's element (tsc's getTupleElementFlags, getTypeFromRestTypeNode)
    if (element->rest && element->optional) {
        TypeNode const* inner = array_element_type_node(&element->type);
        return ty_optional_prop_type(me, inner ? ty_from_node(me, inner, ctx) : type);
    }
    // `...args: Args` of an array type is a rest element of its elements (tsc's createNormalizedTypeReference), a
    // plain array whatever alias it went by
    if (element->rest && type->kind == TY_REFERENCE && ty_is_array(me, type) && type->alias) {
        return ty_array(me, ty_reference_args(me, type).data[0], false);
    }
    // `...string` (TS2574): a variadic element of something that isn't array-like spreads the error type's array (tsc's
    // createNormalizedTupleType: isArrayLikeType && its number index type, else errorType, as a rest element)
    if (element->rest) {
        u32 array_like = TYF_ANY | TYF_TYPE_PARAM | TYF_DEFERRED | TYF_OBJECT | TYF_INTERSECTION | TYF_NEVER;
        TyList parts = type->kind == TY_UNION ? type->composite.types : (TyList){ .data = &type, .len = 1 };
        bool primitive = true;
        for (u32 i = 0; i < parts.len && primitive; i++) primitive = !(parts.data[i]->flags & array_like);
        if (primitive) return ty_array(me, ty_intrinsic(me, TY_ERROR), false);
    }
    return element->optional ? ty_optional_prop_type(me, type) : type;
}

static Ty const* lower_tuple(Checker* me, TypeNode const* node, LowerCtx ctx) {
    u32 arity = (u32)vec_tuple_element_size(&node->tuple.elements);
    // `[...a?: T[]]` is `T[]` like `[...a: T[]]` (tsc's getArrayOrTupleTargetType by the node: its flag is optional)
    if (arity == 1 && vec_tuple_element_at(&node->tuple.elements, 0)->rest &&
        vec_tuple_element_at(&node->tuple.elements, 0)->optional) {
        TypeNode const* inner = array_element_type_node(node);
        if (inner) return ty_array(me, ty_from_node(me, inner, ctx), node->tuple.readonly);
    }
    Ty const* target = tuple_target_of_node(me, node);
    if (!arity) return target;
    Ty const** args = malloc(sizeof(Ty const*) * arity);
    for_each (i, vec_tuple_element, &node->tuple.elements) {
        // TODO: tsc keeps the element type of a rest element (`T` for `...T[]`), this keeps what was written
        args[i_index] = tuple_element_type(me, i, ctx);
    }
    Ty const* ret = reference_from_type_node(me, target, (TyList){ .data = args, .len = arity });
    free(args);
    return ret;
}

// tsc's getArrayElementTypeNode: what `...x: N` spreads if N is syntactically an array
static TypeNode const* array_element_type_node(TypeNode const* node) {
    if (node->tag == TYPE_ARRAY) return node->array.element;
    if (node->tag == TYPE_TUPLE && vec_tuple_element_size(&node->tuple.elements) == 1) {
        TupleElement const* only = vec_tuple_element_at(&node->tuple.elements, 0);
        if (only->rest) return array_element_type_node(&only->type);
    }
    return nullptr;
}

// A rest element whose type isn't written as an array: tsc flags it Variadic (getRestTypeElementFlags)
bool ty_tuple_node_has_variadic(TypeNode const* node) {
    for_each (i, vec_tuple_element, &node->tuple.elements) {
        if (i->rest && !array_element_type_node(&i->type)) return true;
    }
    return false;
}

// Deferred references, see Ty.reference.deferred

// tsc's getTupleElementFlags check in getTypeFromArrayOrTupleTypeNode: `a: ...T` is a named member without a `...`
// of its own, Required there
static bool tuple_node_has_variadic_member(TypeNode const* node) {
    for_each (i, vec_tuple_element, &node->tuple.elements) {
        if (i->rest && !i->rest_in_type && !array_element_type_node(&i->type)) return true;
    }
    return false;
}

// Whether lowering the node could resolve a type alias, going by the syntax alone (tsc's mayResolveTypeAlias): what
// makes a reference inside an alias body deferred
static bool may_resolve_type_alias(Checker* me, TypeNode const* node) {
    switch (node->tag) {
        case TYPE_IDENTIFIER: {
            Symbol const* decl = canonical(me, node->identifier->target);
            // (A built-in type, `object`, `number`, is a keyword node in tsc, no reference)
            if (decl && decl->decl_count && decl->decls[0].kind == SYMBOL_DECL_BUILTIN) return false;
            return decl && (decl->flags & SYMBOL_TYPE_ALIAS);
        }
        case TYPE_TYPEOF: return true;
        case TYPE_KEYOF: return may_resolve_type_alias(me, &node->keyof->interface);
        case TYPE_JSDOC: return may_resolve_type_alias(me, &node->jsdoc->type);
        case TYPE_PREDICATE: return false;
        case TYPE_UNION:
        case TYPE_INTERSECTION:
            for_each (i, vec_type, &node->composite) {
                if (may_resolve_type_alias(me, i)) return true;
            }
            return false;
        case TYPE_INDEXED:
            return may_resolve_type_alias(me, &node->indexed->interface) || may_resolve_type_alias(me, &node->indexed->index);
        case TYPE_TERNARY:
            return may_resolve_type_alias(me, &node->ternary->maybe_sub) ||
                may_resolve_type_alias(me, &node->ternary->maybe_super) ||
                may_resolve_type_alias(me, &node->ternary->if_true) || may_resolve_type_alias(me, &node->ternary->if_false);
        default: return false;
    }
}

// tsc's mayResolveTypeAlias over the elements of a tuple node (a rest element's array type isn't one, its element is)
static bool tuple_elements_may_resolve_type_alias(Checker* me, TypeNode const* node) {
    for_each (i, vec_tuple_element, &node->tuple.elements) {
        // (A rest element of anything but an array type node may: `...T`, `...string[]?`)
        if (i->rest && i->type.tag != TYPE_ARRAY) return true;
        TypeNode const* type = i->rest ? i->type.array.element : &i->type;
        if (may_resolve_type_alias(me, type)) return true;
    }
    return false;
}

// A tuple whose one element is a rest element of an array is that array (tsc's getTupleTargetType): the element
static TypeNode const* lone_rest_array_element(TypeNode const* node) {
    return node->tag == TYPE_TUPLE ? array_element_type_node(node) : nullptr;
}

static Ty const* deferred_reference_target(Checker* me, TypeNode const* node) {
    if (node->tag == TYPE_IDENTIFIER) return ty_declared_type_of(me, canonical(me, node->identifier->target));
    if (node->tag == TYPE_TUPLE && !lone_rest_array_element(node)) return tuple_target_of_node(me, node);
    ty_array(me, ty_intrinsic(me, TY_ANY), false); // Makes sure the targets are looked up
    ty_array(me, ty_intrinsic(me, TY_ANY), true);
    bool readonly = node->tag == TYPE_TUPLE ? node->tuple.readonly : node->array.readonly;
    return readonly ? my readonly_array_target : my array_target;
}

Ty const* ty_deferred_reference(
    Checker* me, TypeNode const* node, LowerCtx ctx, TyMapper const* mapper, Symbol const* alias, TyList alias_args
) {
    // Only what the node mentions is kept of the mapper, so instantiations that differ elsewhere are one type
    u32 kept = 0;
    Ty const* stack_pairs[32];
    Ty const** pairs = stack_pairs;
    bool generic = ctx.generic;
    if (mapper) {
        TyConditionalOuter const* outer = deferred_node_outer(me, node);
        // Whether it mentions type variables isn't known until the arguments resolve, but with every type parameter
        // it mentions mapped to a type without any, it can't (`EP<any, any>`, an interface reference written in the
        // alias EP: not generic, so an `infer` constraint mentioning it is no generic one, see ty_conditional_resolve())
        if (generic && !outer->unknown) {
            generic = false;
            for (u32 j = 0; j < outer->params.len && !generic; j++) {
                Ty const* target = nullptr;
                for (u32 i = 0; i < mapper->len && !target; i++) {
                    if (mapper->sources[i] == outer->params.data[j]) target = mapper->targets[i];
                }
                generic = !target || (target->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES);
            }
        }
        pairs = mapper->len * 2 <= c_arraylen(stack_pairs) ? stack_pairs : malloc(sizeof(Ty const*) * mapper->len * 2);
        for (u32 i = 0; i < mapper->len; i++) {
            bool mentioned = outer->unknown;
            for (u32 j = 0; !mentioned && j < outer->params.len; j++) mentioned = outer->params.data[j] == mapper->sources[i];
            if (!mentioned || mapper->sources[i] == mapper->targets[i]) continue;
            pairs[kept * 2] = mapper->sources[i];
            pairs[kept * 2 + 1] = mapper->targets[i];
            kept++;
        }
    }
    u64 stack_key[64];
    // (The alias is part of the key: an instance of a reference that is an alias's body goes by the alias whose
    // instantiation made it, `L2<1>` and `L1<1>` with `type L1<V> = L2<V>`, see ty_realias())
    u32 count = 2 + kept * 2 + (alias ? 1 + alias_args.len : 0);
    u64* key = count <= c_arraylen(stack_key) ? stack_key : malloc(sizeof(u64) * count);
    key[0] = KEY_DEFERRED_REFERENCE;
    key[1] = (u64)(uintptr_t)node;
    for (u32 i = 0; i < kept * 2; i++) key[2 + i] = pairs[i]->id;
    if (alias) {
        key[2 + kept * 2] = (u64)(uintptr_t)alias;
        for (u32 i = 0; i < alias_args.len; i++) key[3 + kept * 2 + i] = alias_args.data[i]->id;
    }
    Ty const* ret = intern_get(&my links, key, count);
    if (!ret) {
        if (kept != (mapper ? mapper->len : 0)) {
            Ty const** sources = arena_new_n(&my arena, Ty const*, kept ? kept : 1);
            Ty const** targets = arena_new_n(&my arena, Ty const*, kept ? kept : 1);
            for (u32 i = 0; i < kept; i++) {
                sources[i] = pairs[i * 2];
                targets[i] = pairs[i * 2 + 1];
            }
            mapper = kept ? ty_mapper_new(me, (TyList){ .data = sources, .len = kept }, (TyList){ .data = targets, .len = kept }) : nullptr;
        }
        Ty const* target = deferred_reference_target(me, node);
        Ty* fresh = ty_alloc(me, TY_REFERENCE, TYF_OBJECT | (generic ? TYF_COULD_CONTAIN_TYPE_VARIABLES : 0));
        fresh->reference.target = target;
        fresh->reference.state = arena_new(&my arena, TyObjectState);
        TyDeferredRef* deferred = arena_new(&my arena, TyDeferredRef);
        deferred->node = node;
        deferred->ctx = ctx; // Still resolved by the alias, syntactically (tsc's isResolvedByTypeAlias)
        deferred->mapper = mapper;
        deferred->arg_count = target->declared.type_params.len;
        fresh->reference.deferred = deferred;
        // An array or tuple written as the body of an alias goes by the alias name
        fresh->alias = alias;
        fresh->alias_args = ty_list_copy(me, alias_args.data, alias_args.len);
        ret = intern_set(me, &my links, intern_slot(me, &my links, key, count), fresh);
    }
    if (key != stack_key) free(key);
    if (pairs != stack_pairs) free(pairs);
    return ret;
}

// (All of them error types, the outer type parameters' too: tsgo's getTypeArguments repeats errorType for
// len(TypeParameters()), outer and local, so a local interface prints qualified, `f.NumArray<any>`)
static void circular_reference_args(Checker* me, Ty const** args, u32 len) {
    for (u32 i = 0; i < len; i++) args[i] = ty_intrinsic(me, TY_ERROR);
}

// A deferred reference's arguments being resolved (CLAIM_DEFERRED_ARGS, keyed by its TyDeferredRef)
typedef struct DeferredArgsClaim {
    Claim claim;
    bool circular; // Its arguments needed its arguments (TS4109/TS4110 when they're done)
} DeferredArgsClaim;

TyList ty_reference_args(Checker* me, Ty const* reference) {
    check(reference->kind == TY_REFERENCE);
    TyDeferredRef* deferred = reference->reference.deferred;
    if (!deferred) return reference->reference.args;
    u32 len = deferred->arg_count;
    uintptr_t v = sw_get(me, &deferred->args);
    if (__builtin_expect(v != 0, 1)) return (TyList){ .data = (Ty const* const*)v, .len = len };
    switch (sw_look_fast(me, &deferred->args, &deferred->state, &v)) {
        case SW_LOOK_DONE: return (TyList){ .data = (Ty const* const*)v, .len = len };
        case SW_LOOK_MINE: {
            // Only an illegal cycle gets here (its arguments need its arguments; a taint event)
            Ty const** args = arena_new_n(&my arena, Ty const*, len ? len : 1);
            Claim* claim = ty_claim_find(me, CLAIM_DEFERRED_ARGS, deferred, 0);
            if (claim) c_container_of(claim, DeferredArgsClaim, claim)->circular = true;
            circular_reference_args(me, args, len);
            return (TyList){ .data = args, .len = len };
        }
        case SW_LOOK_COMPUTE: break;
    }
    u64 taint = sw_claim_fast(me, &deferred->args, &deferred->state);
    Ty const** args = arena_new_n(&my arena, Ty const*, len ? len : 1);
    DeferredArgsClaim claim = {0};
    ty_claim_push(me, CLAIM_DEFERRED_ARGS, &claim.claim, deferred, 0);
    ty_resolving_push(me, RESOLVING_TYPE_ARGUMENTS, deferred);
    TypeNode const* node = deferred->node;
    if (node->tag == TYPE_IDENTIFIER) {
        Ty const* written_buf[MAX_TYPE_ARGS];
        Ty const* target = reference->reference.target;
        TyList written = lower_type_args(me, target->declared.type_params, target->declared.outer_count,
            &node->identifier->generic_params, deferred->ctx, written_buf, identifier_in_js(node->identifier));
        for (u32 i = 0; i < len; i++) args[i] = ty_instantiate(me, written.data[i], deferred->mapper);
    } else {
        for (u32 i = 0; i < len; i++) {
            TypeNode const* lone = lone_rest_array_element(node);
            Ty const* element = node->tag == TYPE_ARRAY ? ty_from_node(me, node->array.element, deferred->ctx) :
                lone ? ty_from_node(me, lone, deferred->ctx) :
                tuple_element_type(me, vec_tuple_element_at(&node->tuple.elements, i), deferred->ctx);
            args[i] = ty_instantiate(me, element, deferred->mapper);
        }
    }
    if (ty_resolving_pop(me)) claim.circular = true;
    ty_claim_pop(me, CLAIM_DEFERRED_ARGS, &claim.claim);
    if (claim.circular) {
        // tsc's getTypeArguments when popTypeResolution fails: error type arguments, reported at the node (array and
        // tuple nodes have no span here: the current node instead)
        circular_reference_args(me, args, len);
        if (node->tag == TYPE_IDENTIFIER) {
            Symbol const* symbol = reference->reference.target->declared.symbol;
            ty_error_at(me, identifier_type_span(node->identifier), 4109, cstr_d("Type arguments for '",
                symbol ? symbol->name : c_sv("?"), "' circularly reference themselves."));
        } else if (node->tag == TYPE_ARRAY) {
            // (Array and ReadonlyArray have a symbol: tsc's getTypeArguments names it, at the array node)
            Symbol const* symbol = reference->reference.target->declared.symbol;
            csview name = symbol ? symbol->name : c_sv("Array");
            if (span_valid(node->array.span)) {
                ty_error_at(me, node->array.span, 4109, cstr_d("Type arguments for '", name, "' circularly reference themselves."));
            } else {
                ty_error_at_current_node_d(me, 4109, "Type arguments for '", name, "' circularly reference themselves.");
            }
        } else if (node->tag == TYPE_TUPLE && reference->reference.target->declared.kind != TY_DECLARED_TUPLE_TARGET &&
            reference->reference.target->declared.symbol
        ) {
            // (`[...X[]]` is the array X[] (tsc's createNormalizedTupleType): its symbol is named, at the tuple node)
            Symbol const* symbol = reference->reference.target->declared.symbol;
            if (span_valid(node->tuple.span)) {
                ty_error_at(me, node->tuple.span, 4109, cstr_d("Type arguments for '", symbol->name, "' circularly reference themselves."));
            } else {
                ty_error_at_current_node_d(me, 4109, "Type arguments for '", symbol->name, "' circularly reference themselves.");
            }
        } else if (node->tag == TYPE_TUPLE && span_valid(node->tuple.span)) {
            ty_error_at(me, node->tuple.span, 4110, cstr_from("Tuple type arguments circularly reference themselves."));
        } else {
            ty_error_at_current_node_d(me, 4110, "Tuple type arguments circularly reference themselves.");
        }
    }
    TyList list = { .data = args, .len = len };
    SW_FP(TK_DEFERRED_ARGS, deferred, &list, taint);
    (void)list;  // used by the fingerprint build only
    return (TyList){ .data = (Ty const* const*)sw_finish_fast(me, &deferred->args, &deferred->state, (uintptr_t)args, taint), .len = len };
}

// `[a, b, ...rest]`: what `rest` gets out of a tuple, `start` being its index in the pattern. Mutable even of a readonly
// tuple (tsc's sliceTupleType)
static Ty const* binding_rest_elements(Checker* me, Ty const* object, u32 start) {
    TupleShape const* shape = object->reference.target->declared.tuple;
    TyList elements = ty_reference_args(me, object);
    // Past the leading fixed elements: an array of what the elements from there on can be, the empty tuple without
    // any (tsc's sliceTupleType, getRestArrayTypeOfTupleType)
    u32 fixed = 0;
    while (fixed < shape->arity && !(shape->flags[fixed] & TUPLE_ELEMENT_REST)) fixed++;
    if (start > fixed) {
        Ty const* rest = ty_tuple_slice_element_type(me, object, fixed, 0, false);
        return rest ? ty_array(me, rest, false) : ty_tuple_target(me, &(TupleShape){0});
    }
    TupleShape rest_shape = {
        .arity = shape->arity - start,
        .flags = shape->flags + start,
        .names = shape->names ? shape->names + start : nullptr,
        .labels = shape->labels ? shape->labels + start : nullptr,
    };
    return ty_reference(me, ty_tuple_target(me, &rest_shape), (TyList){ .data = elements.data + start, .len = rest_shape.arity });
}

static bool ty_is_tuple_target_empty(Ty const* ty) {
    return ty->kind == TY_DECLARED && ty->declared.kind == TY_DECLARED_TUPLE_TARGET && !ty->declared.tuple->arity;
}

// The elements of a tuple from `start` on, or of each member of a union of tuples; null for anything else (tsc's
// `everyType(sourceType, isTupleType) ? mapType(sourceType, t => sliceTupleType(t, start))` of a rest element)
Ty const* ty_tuple_rest_slice(Checker* me, Ty const* source, u32 start) {
    if (ty_is_tuple(source)) return binding_rest_elements(me, source, start);
    if (source->kind != TY_UNION) return nullptr;
    TyList types = source->composite.types;
    for (u32 i = 0; i < types.len; i++) {
        if (!ty_is_tuple(types.data[i])) return nullptr;
    }
    TyBuf sliced = {0};
    for (u32 i = 0; i < types.len; i++) ty_buf_push(&sliced, binding_rest_elements(me, types.data[i], start));
    Ty const* ret = ty_union(me, sliced.data, sliced.len, nullptr, (TyList){0});
    ty_buf_drop(&sliced);
    return ret;
}

// tsc's getSyntheticElementAccess: `x.y.a` for the `a` of `const { a } = x.y`, and `x.b.c` / `x[0]` for the elements of
// nested patterns (`const { b: { c } } = x`, `const [d] = x`), made once per binding. Null if the initializer isn't a
// reference with a flow node. `flow` is set to that node
static Expr const* synthetic_access(Checker* me, void const* owner, Expr const* object, Value key, Span key_span, FlowNode* flow);
static Expr const* synthetic_element_access(Checker* me, Binding const* binding, FlowNode** flow) {
    Binding const* parent = binding->parent;
    if (!parent || binding->rest) return nullptr;
    Value key;
    Span key_span;
    if (parent->tag == BINDING_OBJECT) {
        ObjectBindingItem const* item = binding->parent_item;
        if (!item) return nullptr;
        if (item->computed_key) {
            // (A computed name of a string or number literal type names the property too: tsc's
            // getDestructuringPropertyName through getLiteralTypeFromPropertyName)
            Ty const* key_type = ty_regular(me, ty_binding_computed_key_type(me, item));
            if (key_type->kind != TY_LITERAL || !(key_type->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL))) return nullptr;
            // (A literal type's string is its atom: once per binding, the synthetic access is remembered)
            key = key_type->flags & TYF_STRING_LITERAL ?
                (Value){ .tag = VALUE_STRING, .string = cstr_from_sv(atom_sv(key_type->literal.string)) } : key_type->literal.value;
            // (Not the key expression's span: a synthetic expression is known by its span, which mustn't be another's)
            key_span = span_valid(item->source_span) ? item->source_span : binding_element_span(binding);
        } else {
            if (item->source.tag != VALUE_STRING && item->source.tag != VALUE_NUMBER) return nullptr;
            key = item->source;
            key_span = item->source_span;
        }
    } else if (parent->tag == BINDING_ARRAY) {
        key = value_number(binding->parent_index);
        key_span = binding_element_span(binding);
    } else {
        return nullptr;
    }
    Expr const* object;
    if (parent->parent) {
        object = synthetic_element_access(me, parent, flow);
        if (!object) return nullptr;
    } else {
        if (!parent->source || parent->source->tag != LAZY_TYPE_INITIALIZED_BINDING) return nullptr;
        InitializedBinding const* initialized = c_container_of(parent->source, InitializedBinding, lazy_type);
        object = initialized_binding_value(initialized);
        *flow = object->tag == EXPR_IDENTIFIER ? object->identifier.flow :
            object->tag == EXPR_PROPERTY ? object->property->flow : nullptr;
        if (!*flow) return nullptr;
    }
    return synthetic_access(me, binding, object, key, key_span, *flow);
}

// `object.key` (or `object[key]`) at `flow`, made once per `owner`
static Expr const* synthetic_access(Checker* me, void const* owner, Expr const* object, Value key, Span key_span, FlowNode* flow) {
    u64 memo_key[2] = { KEY_SYNTHETIC_ACCESS, (u64)(uintptr_t)owner };
    Expr const* access = intern_get(&my links, memo_key, 2);
    if (access) return access;
    PropertyExpr* property = arena_new(&my arena, PropertyExpr);
    // (A dot access when the key is written as its name: its name is its span. `{ "a-b": x }` is `x["a-b"]`)
    bool dot = key.tag == VALUE_STRING && csview_equals_sv(name_cooked(span_sv(key_span)), cstr_sv(&key.string));
    *property = (PropertyExpr){ .expr = *object, .form = dot ? PROPERTY_EXPR_DOT : PROPERTY_EXPR_BRACKET, .flow = flow };
    if (dot) property->name_ = key_span;
    else property->index = expr_literal(key_span, key);
    Expr* synthetic = arena_new(&my arena, Expr);
    *synthetic = (Expr){
        .tag = EXPR_PROPERTY, .property = property, .inner_span = key_span,
        .lazy_type.tag = LAZY_TYPE_EXPR,
    };
    *intern_slot(me, &my links, memo_key, 2) = synthetic;
    return synthetic;
}

// The same for a property of a destructuring assignment `({ a } = x.y)` (tsc's getFlowTypeOfDestructuring of an
// assignment pattern's property): `owner` the pattern's item, `source` the assigned reference
Ty const* ty_destructured_assignment_flow_type(Checker* me, void const* owner, Expr const* source, Value key, Span key_span, Ty const* declared) {
    FlowNode* flow = source->tag == EXPR_IDENTIFIER ? source->identifier.flow : source->tag == EXPR_PROPERTY ? source->property->flow : nullptr;
    if (!flow) return declared;
    return ty_flow_type(me, synthetic_access(me, owner, source, key, key_span, flow), declared, declared, flow);
}

// `const { a } = x.y` after `if (x.y.a)`: an element of a pattern whose initializer is a reference has the flow type of
// `x.y.a` there (tsc's getFlowTypeOfDestructuring)
// The type an object pattern's elements are reported missing from keeps null or undefined (tsc's
// getBindingElementTypeFromParentType: the property of the whole type, which undefined lacks)
static bool pattern_parent_keeps_nullish(Checker* me, Binding const* pattern) {
    Expr const* initializer = pattern->parent ? pattern->default_value : nullptr;
    if (!pattern->parent && pattern->source && pattern->source->tag == LAZY_TYPE_INITIALIZED_BINDING) {
        initializer = initialized_binding_value(c_container_of(pattern->source, InitializedBinding, lazy_type));
        if (initializer->tag == EXPR_IMPLICIT_ANY) initializer = nullptr;
    }
    Ty const* reported = binding_element_parent_type(pattern, initializer);
    return reported && ty_can_be_nullish(me, reported) && ty_non_nullable(me, reported) != reported;
}

static Ty const* destructured_flow_type(Checker* me, Binding const* binding, Ty const* declared) {
    FlowNode* flow = nullptr;
    Expr const* access = synthetic_element_access(me, binding, &flow);
    return access ? ty_flow_type(me, access, declared, declared, flow) : declared;
}

static Ty const* binding_element_of(Checker* me, Binding const* binding, Ty const* object);

Ty const* ty_binding_element_type_from(Checker* me, Binding const* binding, Ty const* object) {
    return binding_element_of(me, binding, object);
}


static bool is_generic_with_undefined_constraint(Checker* me, Ty const* ty) {
    if (ty->kind != TY_TYPE_PARAM && !(ty->flags & TYF_DEFERRED)) return false;
    Ty const* constraint = ty_base_constraint(me, ty);
    if (!constraint || constraint == ty) return false;
    if (constraint->flags & TYF_UNDEFINED) return true;
    if (constraint->kind != TY_UNION) return false;
    for (u32 i = 0; i < constraint->composite.types.len; i++) {
        if (constraint->composite.types.data[i]->flags & TYF_UNDEFINED) return true;
    }
    return false;
}

// Without undefined, a generic type whose constraint has it being its constraint (tsc's getNonUndefinedType):
// `const { a = 1 } = p` with `p: Readonly<P>` and `P extends { a?: number }` makes a number
static Ty const* non_undefined_type(Checker* me, Ty const* ty) {
    TyList members = ty->kind == TY_UNION ? ty->composite.types : (TyList){ .data = &ty, .len = 1 };
    bool some = false;
    for (u32 i = 0; i < members.len && !some; i++) some = is_generic_with_undefined_constraint(me, members.data[i]);
    if (some) {
        TyBuf mapped = {0};
        for (u32 i = 0; i < members.len; i++) {
            Ty const* member = members.data[i];
            bool generic = member->kind == TY_TYPE_PARAM || (member->flags & TYF_DEFERRED);
            Ty const* constraint = generic ? ty_base_constraint(me, member) : nullptr;
            ty_buf_push(&mapped, constraint ? constraint : member);
        }
        ty = ty_union(me, mapped.data, mapped.len, nullptr, (TyList){0});
        ty_buf_drop(&mapped);
    }
    return ty_without_undefined(me, ty);
}

// Whether a key of type `key` names the property `name` (tsc's getRestType: the property's literal key type is
// assignable to the key's). A literal key names it only as the same kind of literal: `0` a property declared with a
// numeric name, `"0"` (or `0` against a string-named one, a tuple's element) not. Numeric names against `number` aren't
// told apart from string ones, TODO
static bool computed_key_names(Checker* me, Ty const* key, Atom const* name, bool numeric_name) {
    if (key->kind == TY_UNION) {
        for (u32 i = 0; i < key->composite.types.len; i++) {
            if (computed_key_names(me, key->composite.types.data[i], name, numeric_name)) return true;
        }
        return false;
    }
    if (key->flags & TYF_ANY) return true;
    bool is_number = false;
    Atom const* named = ty_property_name_of_key(me, key, &is_number);
    if (named) return named == name && is_number == numeric_name;
    return key->kind == TY_INTRINSIC && (key->flags & TYF_STRING) && !ty_unique_symbol_of_name(me, name);
}

// Whether a property's literal key type is a number (tsc's getLiteralTypeFromProperty): declared with a numeric
// literal name, or a computed one of a number literal type
static bool prop_has_numeric_name(Prop const* prop) {
    while (prop->target && !prop_decl_rare(prop->decl)->name_type) prop = prop->target;
    PropDecl const* decl = prop->decl;
    if (prop_decl_rare(decl)->name_type) return (prop_decl_rare(decl)->name_type->flags & TYF_NUMBER_LITERAL) != 0;
    if (!decl->name_is_value || !span_valid(decl->name_span) || !decl->name_span.size) return false;
    char c = decl->name_span.buf[0];
    return (c >= '0' && c <= '9') || c == '.';
}

static Ty const* object_rest_type(Checker* me, Ty const* source, TyList pattern, Symbol const* symbol);

// The annotation a mapped type's property reuses: the one of the property it maps, through mapped modifiers types
// (`Readonly<Pick<P, K>>`: P's, tsc's mapped symbol copying its modifiers property's declarations all the way down,
// like ty_display's mapped members), null if none
static TypeNode const* mapped_prop_annotation(Prop const* prop) {
    Prop const* mapped_from = prop_decl_rare(prop->decl)->mapped_from;
    for (int depth = 0; depth < 16 && mapped_from && mapped_from->mapped && prop_decl_rare(mapped_from->decl)->mapped_from; depth++) {
        mapped_from = prop_decl_rare(mapped_from->decl)->mapped_from;
    }
    if (!mapped_from) return nullptr;
    while (mapped_from->target) mapped_from = mapped_from->target;
    return ty_prop_annotation(mapped_from);
}
Ty const* ty_object_rest_type(Checker* me, Ty const* source, TyList keys) { return object_rest_type(me, source, keys, nullptr); }

// tsc's getRestType makes an anonymous type whose symbol is the rest element's (a binding's): a type of its own, whose
// index signatures name it (getApplicableIndexSymbol: `t2.__index`). Without a symbol, the structural one
static Ty const* rest_literal(
    Checker* me, ObjectLiteralProp const* props, u32 count, IndexInfo const* index, u32 index_count, Symbol const* symbol
) {
    if (!symbol) return ty_object_literal(me, props, count, index, index_count, OBJECT_SYNTHETIC);
    Ty const* ret = ty_object_literal_of(me, props, count, index, index_count, OBJECT_SYNTHETIC, symbol);
    u64 key[2] = { KEY_REST_SYMBOL, (u64)(uintptr_t)ret };
    void const** slot = intern_slot(me, &me->links, key, 2);
    // (Its anonymous.rest_binding is the symbol too, set when it was made: ty_object_literal_alternative())
    if (!*slot) *slot = symbol;
    return ret;
}

Symbol const* ty_rest_type_symbol(Checker* me, Ty const* type) {
    return intern_get(&me->links, (u64[]){ KEY_REST_SYMBOL, (u64)(uintptr_t)type }, 2);
}

// Whether the keys a pattern names take the property `name` out of its rest
static bool pattern_names(Checker* me, TyList keys, Atom const* name, bool numeric_name) {
    for (u32 i = 0; i < keys.len; i++) {
        if (computed_key_names(me, keys.data[i], name, numeric_name)) return true;
    }
    return false;
}

Ty const* ty_binding_computed_key_type(Checker* me, ObjectBindingItem const* item) {
    u64 key[2] = { KEY_BINDING_COMPUTED_KEY, (u64)(uintptr_t)item };
    Ty const* cached = intern_get(&my links, key, 2);
    if (cached == (Ty const*)&my links) {
        TY_WORKER_STATE(me); // (In flight here)
        return ty_empty_object_type(me);
    }
    if (cached) return cached;
    *intern_slot(me, &my links, key, 2) = (Ty const*)&my links;
    Ty const* ret = ty_expr_type(me, item->computed_key);
    *intern_slot(me, &my links, key, 2) = ret;
    return ret;
}

Ty const* ty_binding_access_key(Checker* me, ObjectBindingItem const* item) {
    return intern_get(&my links, (u64[]){ KEY_BINDING_ACCESS_KEY, (u64)(uintptr_t)item }, 2);
}

// The keys an object pattern names before its rest: literal types of its names, the types of its computed ones
static TyList pattern_keys(Checker* me, Binding const* pattern) {
    u32 count = 0;
    for_each (item, vec_object_binding_item, &pattern->object) count += !item->target.rest;
    Ty const** keys = arena_new_n(&my arena, Ty const*, count ? count : 1);
    u32 len = 0;
    for_each (item, vec_object_binding_item, &pattern->object) {
        if (item->target.rest) continue;
        keys[len++] = item->private_name ? ty_intrinsic(me, TY_NEVER) :
            item->computed_key ? ty_binding_computed_key_type(me, item) : ty_literal(me, &item->source);
    }
    return (TyList){ .data = keys, .len = len };
}

// The annotation a property was declared with, which the types baseline reuses for a copy of it where it still says
// the same (ObjectLiteralProp.asserted)
TypeNode const* ty_prop_annotation(Prop const* prop) {
    PropDecl const* decl = prop->decl;
    TypeNode const* asserted = prop_decl_asserted(decl);
    if (asserted) return asserted;
    return decl->node && decl->node->parsed && !decl->lazy && !decl->function && !prop_decl_rare(decl)->overload_count && !decl->symbol ? decl->node : nullptr;
}

// The rest of an intersection: the properties of all of its members, each of the intersection's type for it
static Ty const* intersection_rest_type(Checker* me, Ty const* source, TyList pattern, Symbol const* symbol) {
    TyList parts = source->composite.types;
    for (u32 i = 0; i < parts.len; i++) {
        Ty const* part = ty_apparent_type(me, parts.data[i]);
        // (A generic member keeps the whole thing, like a generic object: TODO Omit)
        if (!(part->flags & TYF_OBJECT) || ty_members(me, part)->mapped) return source;
    }
    ObjectLiteralProp* props = nullptr;
    u32 count = 0, cap = 0;
    for (u32 i = 0; i < parts.len; i++) {
        Members const* members = ty_members(me, ty_apparent_type(me, parts.data[i]));
        for (u32 j = 0; j < members->prop_count; j++) {
            Atom const* name = members->props[j]->name;
            bool seen = false;
            for (u32 k = 0; k < count && !seen; k++) seen = props[k].name == name;
            if (seen || pattern_names(me, pattern, name, prop_has_numeric_name(members->props[j]))) continue;
            FoundProp found = ty_find_prop(me, source, name);
            if (!found.found || (found.flags & (PROP_PRIVATE | PROP_PROTECTED))) continue;
            if (found.prop && !ty_is_spreadable_prop(me, found.prop)) continue;
            // (One member's property, not the intersection's own: the same property unless that makes it writable,
            // tsc's getSpreadSymbol)
            Prop const* single = members->props[j];
            for (u32 k = i + 1; k < parts.len && single; k++) {
                if (ty_get_prop(me, ty_apparent_type(me, parts.data[k]), name)) single = nullptr;
            }
            if (single && !(single->flags & PROP_READONLY) && found.type == prop_type(me, single)) {
                if (count == cap) {
                    cap = cap ? cap * 2 : 8;
                    props = realloc(props, sizeof(ObjectLiteralProp) * cap);
                    panic_if(!props, "out of memory");
                }
                props[count++] = (ObjectLiteralProp){ .reuse = single };
                continue;
            }
            // (Declared where the first declared property it comes from is; a mapped type's property reuses the
            // annotation of the property it maps, like object_rest_type())
            Prop const* origin = found.prop;
            while (origin && origin->target && !origin->decl->decl_span_count) origin = origin->target;
            TypeNode const* annotation = origin ? ty_prop_annotation(origin) : nullptr;
            if (!annotation && found.prop) annotation = mapped_prop_annotation(found.prop);
            if (count == cap) {
                cap = cap ? cap * 2 : 8;
                props = realloc(props, sizeof(ObjectLiteralProp) * cap);
                panic_if(!props, "out of memory");
            }
            props[count++] = (ObjectLiteralProp){
                .name = name,
                .flags = found.flags & (u32)~(PROP_READONLY | PROP_GETTER | PROP_SETTER | PROP_ACCESSOR | PROP_CLASS_GETTER),
                .type = found.type,
                .name_span = origin ? origin->decl->name_span : (Span){0},
                .decl_spans = origin ? origin->decl->decl_spans : nullptr,
                .decl_span_count = origin ? origin->decl->decl_span_count : 0,
                .name_is_value = origin && origin->decl->name_is_value,
                .name_form = found.prop ? ty_prop_name_form_of(found.prop) : 0,
                .asserted = annotation,
                .spread_variable = origin ? origin->decl->symbol : nullptr,
            };
        }
    }
    // (The intersection's index signatures stay: tsc's getRestType with getIndexInfosOfType)
    Members const* own = ty_members(me, source);
    Ty const* ret = rest_literal(me, props, count, own->index, own->index_count, symbol);
    free(props);
    return ret;
}

// The rest of a generic object (or of a pattern with a generic computed name): `Omit<T, names>`, the names being those
// the pattern takes and those of the properties a rest leaves out anyway (private, protected, class methods). T itself
// if that's no names (tsc's getRestType)
static Ty const* generic_rest_type(Checker* me, Ty const* source, TyList pattern) {
    TyBuf keys = {0};
    for (u32 i = 0; i < pattern.len; i++) ty_buf_push(&keys, pattern.data[i]);
    Ty const* apparent = ty_apparent_type(me, source);
    if (apparent->flags & (TYF_OBJECT | TYF_INTERSECTION)) {
        Members const* members = ty_members(me, apparent);
        for (u32 i = 0; i < members->prop_count; i++) {
            Prop const* prop = members->props[i];
            // (tsc's getLiteralTypeFromProperty of every property, in order: the creation order its union goes by)
            if (ty_is_spreadable_prop(me, prop)) continue;
            // (A private or protected member, or a `#x`, has no key to omit: keyof lacks it already. tsc's
            // getLiteralTypeFromProperty gives never)
            if ((prop->flags & (PROP_PRIVATE | PROP_PROTECTED)) || ty_private_name_class(me, prop->name)) continue;
            ty_buf_push(&keys, ty_prop_name_literal(me, prop->name));
        }
    }
    Ty const* omitted = keys.len ? ty_union(me, keys.data, keys.len, nullptr, (TyList){0}) : nullptr;
    ty_buf_drop(&keys);
    if (!omitted || (omitted->flags & TYF_NEVER)) return source;
    Symbol const* omit = ty_global_type_alias_reported(me, c_sv("Omit"), 2);
    if (!omit) return ty_intrinsic(me, TY_ERROR);
    Ty const* args[2] = { source, omitted };
    return ty_alias_instance(me, omit, (TyList){ .data = args, .len = 2 });
}

static bool pattern_has_generic_key(Checker* me, TyList pattern) {
    (void)me;
    for (u32 i = 0; i < pattern.len; i++) {
        if (ty_is_generic_index(pattern.data[i])) return true;
    }
    return false;
}

// Whether `...rest` of an object pattern can be taken of `source` (else TS2700): not unknown, never or a primitive
bool ty_object_rest_source_valid(Checker* me, Ty const* source) {
    return (source->flags & TYF_ANY) || (!(source->flags & TYF_UNKNOWN) && ty_is_valid_spread_type(me, source));
}

// tsc's getRestType: `...rest` of an object pattern, without the properties the pattern names, private and protected
// members and class methods, and with nothing readonly
static Ty const* object_rest_type(Checker* me, Ty const* source, TyList pattern, Symbol const* symbol) {
    if (source->kind == TY_UNION) {
        Ty const* stack[16];
        TyList types = source->composite.types;
        Ty const** mapped = types.len <= c_arraylen(stack) ? stack : malloc(sizeof(Ty const*) * types.len);
        u32 len = 0;
        for (u32 i = 0; i < types.len; i++) {
            if (!(types.data[i]->flags & TYF_NULLABLE)) mapped[len++] = object_rest_type(me, types.data[i], pattern, symbol);
        }
        // (Nothing left is tsc's emptyObjectType, which has no symbol: no inferable index)
        Ty const* ret = len ? ty_union(me, mapped, len, nullptr, (TyList){0}) : ty_empty_object_type(me);
        if (mapped != stack) free((void*)mapped);
        return ret;
    }
    if (source->flags & TYF_NEVER) return ty_empty_object_type(me);
    if (ty_is_generic_object(me, source) || pattern_has_generic_key(me, pattern)) return generic_rest_type(me, source, pattern);
    Ty const* apparent = ty_apparent_type(me, source);
    if (apparent->kind == TY_INTERSECTION && !ty_is_generic_index(source)) return intersection_rest_type(me, apparent, pattern, symbol);
    if (!(apparent->flags & TYF_OBJECT) || ty_is_generic_index(source)) return source;
    Members const* members = ty_members(me, apparent);
    if (members->mapped) return source;
    ObjectLiteralProp* props = malloc(sizeof(ObjectLiteralProp) * (members->prop_count ? members->prop_count : 1));
    panic_if(!props, "out of memory");
    u32 count = 0;
    for (u32 i = 0; i < members->prop_count; i++) {
        Prop const* prop = members->props[i];
        if (!ty_is_spreadable_prop(me, prop)) continue;
        if (pattern_names(me, pattern, prop->name, prop_has_numeric_name(prop))) continue;
        // (tsc's getSpreadSymbol: the same property unless that makes it writable)
        if (!(prop->flags & PROP_READONLY)) {
            props[count++] = (ObjectLiteralProp){ .reuse = prop };
            continue;
        }
        Prop const* declared = prop;
        while (declared->target && !declared->decl->decl_span_count) declared = declared->target;
        // (A mapped type's property is declared where the property it maps is: tsc's mapped symbol's declarations)
        TypeNode const* annotation = ty_prop_annotation(declared);
        if (!annotation) annotation = mapped_prop_annotation(prop);
        props[count++] = (ObjectLiteralProp){
            .name = prop->name,
            .flags = (prop->flags & (u32)~(PROP_READONLY | PROP_GETTER | PROP_SETTER | PROP_ACCESSOR | PROP_CLASS_GETTER)) | PROP_NO_VALUE_DECLARATION,
            .type = prop_type(me, prop),
            .name_span = declared->decl->name_span,
            .name_form = ty_prop_name_form_of(declared),
            .decl_spans = declared->decl->decl_spans,
            .decl_span_count = declared->decl->decl_span_count,
            .name_is_value = declared->decl->name_is_value,
            .asserted = annotation,
        };
    }
    Ty const* ret = rest_literal(me, props, count, members->index, members->index_count, symbol);
    free(props);
    return ret;
}

// An object literal contextually typed by a binding pattern's implied type also has the pattern's properties it
// leaves out (tsc's checkObjectLiteral with contextualTypeHasPattern): `{ a: { b = 1 } = {} }` makes the default a
// `{ b?: number }`. Only where the default makes the element's type: the literal itself prints as written.
// TODO: TS2525 for a property without a default
static Ty const* with_pattern_props(Checker* me, Ty const* type, Ty const* pattern) {
    if (type->kind != TY_ANONYMOUS || !(type->flags & TYF_OBJECT) || !(pattern->flags & TYF_OBJECT)) return type;
    Members const* have = ty_members(me, type);
    Members const* implied = ty_members(me, pattern);
    ObjectLiteralProp* props = malloc(sizeof(ObjectLiteralProp) * (have->prop_count + implied->prop_count + 1));
    panic_if(!props, "out of memory");
    u32 count = 0;
    for (u32 i = 0; i < have->prop_count; i++) props[count++] = (ObjectLiteralProp){ .reuse = have->props[i] };
    bool added = false;
    for (u32 i = 0; i < implied->prop_count; i++) {
        if (ty_get_object_prop(me, type, implied->props[i]->name)) continue;
        props[count++] = (ObjectLiteralProp){ .reuse = implied->props[i] };
        added = true;
    }
    Ty const* ret = added ? ty_object_literal(me, props, count, have->index, have->index_count, OBJECT_LITERAL_FRESH) : type;
    free(props);
    return ret;
}

// Whether a pattern element belongs to a `const` (or `using`) declaration, through any of its names
static bool binding_in_const_declaration(Binding const* element) {
    if (element->tag == BINDING_IDENTIFIER) return element->symbol && element->symbol->is_const;
    if (element->tag == BINDING_ARRAY) {
        for_each (item, vec_binding, &element->array) if (item->tag != BINDING_NOOP) return binding_in_const_declaration(item);
    }
    if (element->tag == BINDING_OBJECT) {
        for_each (item, vec_object_binding_item, &element->object) return binding_in_const_declaration(&item->target);
    }
    return false;
}

// tsc's getTypeFromBindingElement: a default's type widened (and optional; but not its literals in a `const`: tsc's
// widenTypeInferredFromInitializer), a nested pattern's own type, else any
// `literals_only`: only its literals widen (tsc's widenTypeInferredFromInitializer); the declared type of what the
// pattern declares is widened whole (getWidenedTypeForVariableLikeDeclaration), which is what it is for otherwise
static Ty const* binding_element_implied_type(Checker* me, Binding const* element, bool include_pattern, bool literals_only) {
    if (element->default_value) {
        // (Not remembered while the pattern is a contextual type: its sibling references are `any` then)
        if (include_pattern) { TAINT_FLOW(); my flow_uncached++; }
        bool nested = (element->tag == BINDING_OBJECT && vec_object_binding_item_size(&element->object)) ||
            (element->tag == BINDING_ARRAY && vec_binding_size(&element->array));
        Ty const* type;
        if (nested) {
            Ty const* pattern = ty_binding_pattern_type(me, element, true);
            type = ty_check_expr(me, element->default_value, pattern);
            if (element->tag == BINDING_OBJECT && element->default_value->tag == EXPR_OBJECT) {
                type = with_pattern_props(me, type, pattern);
            }
        } else {
            type = ty_expr_type(me, element->default_value);
        }
        // (`[]` is undefined[] in a contextual type without strictNullChecks, any[] in the declared type)
        if (include_pattern || literals_only) {
            if (!binding_in_const_declaration(element)) type = ty_widened_literal(me, type);
        } else {
            type = ty_widened_for_declaration(me, type, !binding_in_const_declaration(element));
        }
        if (include_pattern) { TAINT_FLOW(); my flow_uncached--; }
        if (!compiler_options()->strict_null_checks) return type;
        Ty const* parts[2] = { type, ty_intrinsic(me, TY_UNDEFINED) };
        return ty_union(me, parts, 2, nullptr, (TyList){0});
    }
    if (element->tag == BINDING_OBJECT || element->tag == BINDING_ARRAY) return ty_binding_pattern_type(me, element, include_pattern);
    // (While the pattern is a contextual type, an any inference never infers directly: tsc's nonInferrableAnyType)
    return ty_intrinsic(me, include_pattern ? TY_NON_INFERRABLE_ANY : TY_ANY);
}

// tsc's getTypeFromBindingPattern: what an unannotated `{ a, b = 1 }` or `[a, b = 1]` parameter takes, `{ a: any; b?:
// number }` or `[any, number?]`
// A copy of a type under a new id, sharing its lazy state (TyObjectState and the like): the members of a pattern's
// clone are the original's, whichever of the two resolves them, which is a write the original's other readers see.
// That is history-like the way K1's order is (a tuple reference's this-argument is whichever resolved them first) and
// safe under sharing: the state protocol is per state, not per Ty. Its order_key is its own: copying a cached one
// would make it depend on whether the original was compared before it was cloned (the key can hold the id)
static_assert(offsetof(Ty, height) < offsetof(Ty, flags) && offsetof(Ty, flags) < offsetof(Ty, order_key));
static Ty* clone_with_new_id(Checker* me, Ty const* ty) {
    Ty* ret = ty_alloc(me, (TyKind)ty->kind, ty->flags);
    u32 id = ret->id;
    // (Around order_key, which another worker may be caching in the original meanwhile: an atomic word, plan 86 4f)
    size_t at = offsetof(Ty, order_key), size = ty_size_of_kind((TyKind)ty->kind);
    // (And around height, which another worker may be memoizing in the original: TTTT_NESTING_ORDER=content)
    memcpy(ret, ty, offsetof(Ty, height));
    memcpy((char*)ret + offsetof(Ty, flags), (char const*)ty + offsetof(Ty, flags), at - offsetof(Ty, flags));
    ret->height = 0;
    memcpy((char*)ret + at + sizeof ret->order_key, (char const*)ty + at + sizeof ret->order_key, size - at - sizeof ret->order_key);
    ret->id = id;
    ret->order_key = 0;
    return ret;
}

// A clone of the type marked as a pattern's (tsc's cloneTypeReference, `pattern`)
Ty const* ty_pattern_clone(Checker* me, Ty const* ty) {
    Ty* ret = clone_with_new_id(me, ty);
    ret->flags |= TYF_BINDING_PATTERN;
    return ret;
}

Ty const* ty_pattern_unclone(Checker* me, Ty const* ty) {
    Ty* ret = clone_with_new_id(me, ty);
    ret->flags &= ~(u32)TYF_BINDING_PATTERN;
    return ret;
}

static Ty const* binding_pattern_type(Checker* me, Binding const* pattern, bool include_pattern);

// What tsc's getTypeFromArrayBindingPattern asks for an empty or rest-only array pattern, from ES2015 on: TS2318 once,
// without a position, when the lib has no Iterable (createIterableType, getGlobalIterableType(/*reportErrors*/ true)).
// Only where tsc makes that type: tttt makes pattern types in places tsc doesn't (an initializer's contextual type that
// nothing asks for)
void ty_report_pattern_iterable(Checker* me, Binding const* pattern) {
    if (my iterable_reported || pattern->tag != BINDING_ARRAY) return;
    u32 len = (u32)vec_binding_size(&pattern->array);
    if (len > 1 || (len == 1 && !vec_binding_at(&pattern->array, 0)->rest)) return;
    if (program_get_global(my program, c_sv("Iterable"), SYMBOL_TYPE)) return;
    my iterable_reported = true;
    error_ctx_global_error_d(my errs, 2318, "Cannot find global type 'Iterable'.");
}

Ty const* ty_binding_pattern_type(Checker* me, Binding const* pattern, bool include_pattern) {
    bool pushed = include_pattern && my contextual_pattern_count < c_arraylen(my contextual_patterns);
    if (pushed) my contextual_patterns[my contextual_pattern_count++] = pattern;
    Ty const* ret = binding_pattern_type(me, pattern, include_pattern);
    if (pushed) my contextual_pattern_count--;
    return ret;
}

static Ty const* binding_pattern_type(Checker* me, Binding const* pattern, bool include_pattern) {
    if (pattern->tag == BINDING_OBJECT) {
        ObjectLiteralProp* props = nullptr;
        u32 count = 0, cap = 0;
        IndexInfo index = {0};
        u32 index_count = 0;
        bool has_computed = false;
        for_each (item, vec_object_binding_item, &pattern->object) {
            if (item->target.rest) {
                index = (IndexInfo){ .key = ty_intrinsic(me, TY_STRING), .value = ty_intrinsic(me, TY_ANY),
                    .param_name = my atoms.x };
                index_count = 1;
                continue;
            }
            // A computed name of a literal or unique symbol type is a property like any other (tsc's
            // isTypeUsableAsPropertyName)
            bool is_number = false;
            if (item->private_name) continue;
            Ty const* key = item->computed_key ? ty_binding_computed_key_type(me, item) : ty_literal(me, &item->source);
            Atom const* name = ty_property_name_of_key(me, key, &is_number);
            if (!name) {
                has_computed |= item->computed_key != nullptr;
                continue;
            }
            ObjectLiteralProp prop = {
                .name = name,
                .flags = item->target.default_value ? PROP_OPTIONAL : 0,
                .type = binding_element_implied_type(me, &item->target, include_pattern, false), // (tsc's symbol has no declaration)
            };
            // (A name the pattern takes twice is one property, the last one's: tsc's members.set)
            u32 at = 0;
            while (at < count && props[at].name != name) at++;
            if (at < count) {
                props[at] = prop;
                continue;
            }
            if (count == cap) {
                cap = cap ? cap * 2 : 8;
                props = realloc(props, sizeof(ObjectLiteralProp) * cap);
                panic_if(!props, "out of memory");
            }
            props[count++] = prop;
        }
        // (tsc's getTypeFromObjectBindingPattern makes it without a symbol: no inferable index)
        Ty const* ret = ty_object_literal(me, props, count, index_count ? &index : nullptr, index_count,
            OBJECT_SYNTHETIC | OBJECT_NO_INFERABLE_INDEX | OBJECT_NO_SYMBOL);
        free(props);
        if (!include_pattern) return ret;
        ret = ty_pattern_clone(me, ret);
        // (tsc's ObjectLiteralPatternWithComputedProperties: its literal gets no excess property check)
        if (has_computed) *intern_slot(me, &my links, (u64[]){ KEY_PATTERN_COMPUTED, ret->id }, 2) = ret;
        return ret;
    }
    if (pattern->tag != BINDING_ARRAY) return ty_intrinsic(me, TY_ANY);
    u32 len = (u32)vec_binding_size(&pattern->array);
    Binding const* last = len ? vec_binding_at(&pattern->array, len - 1) : nullptr;
    bool has_rest = last && last->rest;
    if (!len || (len == 1 && has_rest)) {
        // Anything iterable
        Symbol const* iterable = program_get_global(my program, c_sv("Iterable"), SYMBOL_TYPE);
        Ty const* declared = iterable ? ty_declared_type_of(me, iterable) : nullptr;
        Ty const* any = ty_intrinsic(me, TY_ANY);
        if (declared && declared->kind == TY_DECLARED && declared->declared.type_params.len) {
            // (tsc's createIterableType: Iterable<any, void, undefined>)
            TyList params = declared->declared.type_params;
            Ty const** args = arena_new_n(&my arena, Ty const*, params.len);
            for (u32 i = 0; i < params.len; i++) {
                args[i] = i == 1 ? ty_intrinsic(me, TY_VOID) : i == 2 ? ty_intrinsic(me, TY_UNDEFINED) : any;
            }
            return ty_reference(me, declared, (TyList){ .data = args, .len = params.len });
        }
        return ty_array(me, any, false);
    }
    // Required up to the last element that has no default and isn't a hole or the rest
    u32 min = 0;
    for (u32 i = 0; i < len; i++) {
        Binding const* element = vec_binding_at(&pattern->array, i);
        if (!(element->rest || element->tag == BINDING_NOOP || element->default_value)) min = i + 1;
    }
    Ty const** elements = arena_new_n(&my arena, Ty const*, len);
    unsigned char* flags = calloc(len, 1);
    panic_if(!flags, "out of memory");
    for (u32 i = 0; i < len; i++) {
        Binding const* element = vec_binding_at(&pattern->array, i);
        elements[i] = element->tag == BINDING_NOOP ? ty_intrinsic(me, TY_ANY) : binding_element_implied_type(me, element, include_pattern, false);
        if (element->rest) {
            flags[i] = TUPLE_ELEMENT_REST;
            elements[i] = ty_array(me, elements[i], false);
        } else if (i >= min) {
            flags[i] = TUPLE_ELEMENT_OPTIONAL;
        }
    }
    TupleShape shape = { .arity = len, .flags = flags };
    Ty const* ret = ty_reference(me, ty_tuple_target(me, &shape), (TyList){ .data = elements, .len = len });
    free(flags);
    return include_pattern ? ty_pattern_clone(me, ret) : ret;
}

Ty const* ty_pad_initializer_type(Checker* me, Ty const* type, Binding const* pattern) {
    if (!binding_is_parameter(pattern)) return type;
    if (pattern->tag == BINDING_OBJECT && type->kind == TY_ANONYMOUS && type->anonymous.object_literal) {
        // The elements the literal lacks are optional properties, of their default's type or any (tsgo's
        // padObjectLiteralType: getTypeFromBindingElement)
        Members const* members = ty_members(me, type);
        ObjectLiteralProp* props = nullptr;
        u32 count = 0;
        for_each (item, vec_object_binding_item, &pattern->object) {
            if (item->target.rest || item->computed_key || item->private_name) continue;
            bool is_number = false;
            Atom const* name = ty_property_name_of_key(me, ty_literal(me, &item->source), &is_number);
            if (!name || ty_get_prop(me, type, name)) continue;
            if (!props) {
                props = malloc(sizeof(ObjectLiteralProp) * (members->prop_count + vec_object_binding_item_size(&pattern->object)));
                panic_if(!props, "out of memory");
                for (u32 i = 0; i < members->prop_count; i++) props[count++] = (ObjectLiteralProp){ .reuse = members->props[i] };
            }
            props[count++] = (ObjectLiteralProp){
                .name = name,
                .flags = PROP_OPTIONAL,
                .type = binding_element_implied_type(me, &item->target, false, true),
            };
        }
        if (!props) return type;
        Ty const* ret = ty_object_literal(me, props, count, members->index, members->index_count,
            (type->flags & TYF_FRESH_OBJECT ? OBJECT_LITERAL_FRESH : OBJECT_LITERAL) |
            (type->kind == TY_ANONYMOUS && type->anonymous.js_literal ? OBJECT_JS : 0) |
            (type->kind == TY_ANONYMOUS && type->anonymous.contains_spread ? OBJECT_CONTAINS_SPREAD : 0));
        free(props);
        return ret;
    }
    if (pattern->tag != BINDING_ARRAY) return type;
    // A tuple shorter than the pattern gets optional elements: the defaults' types, else any (tsc's padTupleType)
    TupleShape const* shape = ty_tuple_shape(type);
    u32 len = (u32)vec_binding_size(&pattern->array);
    if (!shape || shape->arity >= len) return type;
    for (u32 i = 0; i < shape->arity; i++) {
        if (shape->flags[i] & TUPLE_ELEMENT_REST) return type;
    }
    TyList args = shape->arity ? ty_reference_args(me, type) : (TyList){0};
    Ty const** elements = arena_new_n(&my arena, Ty const*, len);
    unsigned char* flags = calloc(len, 1);
    panic_if(!flags, "out of memory");
    for (u32 i = 0; i < shape->arity; i++) {
        elements[i] = args.data[i];
        flags[i] = shape->flags[i];
    }
    u32 arity = shape->arity;
    for (u32 i = shape->arity; i < len; i++) {
        Binding const* element = vec_binding_at(&pattern->array, i);
        if (i == len - 1 && element->rest) break;
        elements[arity] = element->tag != BINDING_NOOP && element->default_value ?
            binding_element_implied_type(me, element, false, true) : ty_intrinsic(me, TY_ANY);
        flags[arity++] = TUPLE_ELEMENT_OPTIONAL;
    }
    TupleShape padded = { .arity = arity, .flags = flags, .readonly = shape->readonly };
    Ty const* ret = ty_reference(me, ty_tuple_target(me, &padded), (TyList){ .data = elements, .len = arity });
    free(flags);
    return ret;
}

// The declaration the root of a pattern belongs to, if it has an initializer or can be a parameter
static InitializedBinding const* root_initialized(Binding const* root) {
    if (!root->source || root->source->tag != LAZY_TYPE_INITIALIZED_BINDING) return nullptr;
    return c_container_of(root->source, InitializedBinding, lazy_type);
}

static Ty const* binding_element_context(Checker* me, Binding const* element);

// tsc's getContextualTypeForVariableLikeDeclaration: what a pattern (or one of its elements) is expected to be
static Ty const* binding_declared_context(Checker* me, Binding const* binding) {
    if (binding->parent) return binding_element_context(me, binding);
    if (binding->has_explicit_type) return ty_from_node(me, binding_type(binding), (LowerCtx){ .generic = true });
    InitializedBinding const* initialized = root_initialized(binding);
    return initialized && binding_is_parameter(binding) ? ty_contextual_parameter_type(me, initialized) : nullptr;
}

// tsc's getContextualTypeForBindingElement: the element's property (or element) of what its pattern is expected to be,
// or of the pattern's initializer
static Ty const* binding_element_context(Checker* me, Binding const* element) {
    Binding const* pattern = element->parent;
    Ty const* parent_type = binding_declared_context(me, pattern);
    InitializedBinding const* initialized = pattern->parent ? nullptr : root_initialized(pattern);
    if (!parent_type && initialized && initialized_binding_value(initialized)->tag != EXPR_IMPLICIT_ANY) {
        parent_type = ty_pad_initializer_type(me, ty_expr_type(me, initialized_binding_value(initialized)), pattern);
    }
    if (!parent_type) return nullptr;
    if (pattern->tag == BINDING_ARRAY) {
        // (An element's name is itself when there is no property name: a pattern has no context from here)
        if (element->tag == BINDING_OBJECT || element->tag == BINDING_ARRAY) return nullptr;
        return ty_contextual_element_type(me, parent_type, element->parent_index);
    }
    ObjectBindingItem const* item = element->parent_item;
    if (item->private_name) return nullptr;
    Ty const* key = item->computed_key ? ty_binding_computed_key_type(me, item) : ty_literal(me, &item->source);
    Atom const* name = ty_literal_name(me, key);
    if (!name) return nullptr;
    FoundProp found = ty_find_prop(me, parent_type, name);
    return found.found ? found.type : nullptr;
}

Ty const* ty_binding_default_type(Checker* me, Binding const* element) {
    Ty const* contextual = binding_element_context(me, element);
    // (tsc's getContextualTypeForInitializerExpression: else what a non-empty pattern implies)
    if (!contextual && ((element->tag == BINDING_OBJECT && vec_object_binding_item_size(&element->object)) ||
        (element->tag == BINDING_ARRAY && vec_binding_size(&element->array)))
    ) {
        contextual = ty_binding_pattern_type(me, element, true);
    }
    return ty_check_expr(me, element->default_value, contextual);
}

static Ty const* binding_assigned_type(Checker* me, Binding const* binding);

// What is assigned to a binding: the initializer for the root of a pattern, for an element the property or element
// of what its pattern is assigned
Ty const* ty_binding_assigned_type(Checker* me, Binding const* binding) {
    Ty const* ret = binding_assigned_type(me, binding);
    // In JavaScript an element with a default under no written type that is still an empty array literal's type is
    // any[], which noImplicitAny reports (tsc's getTypeForBindingElement: widenTypeInferredFromInitializer,
    // isEmptyArrayLiteralType, TS7031)
    if (!binding->parent || !binding->default_value || binding->tag != BINDING_IDENTIFIER) return ret;
    SourceFile const* file = span_source(binding->content_span);
    if (!file || !file->is_js || !ty_is_array(me, ret)) return ret;
    Binding const* root = binding->parent;
    while (root->parent) root = root->parent;
    if (root->has_explicit_type) return ret;
    TyList args = ty_reference_args(me, ret);
    if (args.len != 1 ||
        args.data[0] != ty_intrinsic(me, compiler_options()->strict_null_checks ? TY_IMPLICIT_NEVER : TY_UNDEFINED_WIDENING)
    ) {
        return ret;
    }
    TY_WORKER_STATE(me);
    *intern_slot(me, &my links, (u64[]){ KEY_ELEMENT_WIDENED_ANY, (u64)(uintptr_t)binding }, 2) = ret;
    return ty_array(me, ty_intrinsic(me, TY_ANY), false);
}

// The type of a pattern's element in the type `object` destructured, before the element is widened and its default
// applied (tsc's getBindingElementTypeFromParentType)
static Ty const* binding_element_of(Checker* me, Binding const* binding, Ty const* object) {
    Binding const* parent = binding->parent;
    bool in_array = parent->tag == BINDING_ARRAY;
    // An array pattern takes elements of an array (or tuple) by index, of anything else what iterating it gives
    // (tsc's getTypeForBindingElement). What an element lacks is reported by binding_typecheck()
    bool array_like = in_array && (object->flags & TYF_ANY || ty_extends_array(me, object));
    Ty const* ret;
    if (in_array && !array_like) {
        Ty const* element = ty_iterated_type(me, ITERATION_DESTRUCTURING, object, span_null, nullptr);
        ret = binding->rest ? ty_array(me, element, false) : ty_possibly_out_of_bounds(me, element);
    } else if (binding->rest && in_array) {
        // Of a tuple (or a union of them, or a type variable constrained to one), the tuple of the elements from here
        // on; of anything else a mutable array of its elements (tsc's getTypeForBindingElement)
        Ty const* base = object;
        if (object->kind == TY_TYPE_PARAM || (object->flags & TYF_DEFERRED)) {
            Ty const* constraint = ty_base_constraint(me, object);
            if (constraint && constraint != object && !(constraint->flags & TYF_UNKNOWN)) base = constraint;
        }
        // (Of any, any: tsc's getBindingElementTypeFromParentType. Not of the any a `= null` initializer widens to,
        // whose unwidened null tsc goes by)
        bool any_parent = (object->flags & TYF_ANY) != 0;
        if (any_parent && !parent->parent && parent->source && parent->source->tag == LAZY_TYPE_INITIALIZED_BINDING) {
            InitializedBinding const* initialized = c_container_of(parent->source, InitializedBinding, lazy_type);
            if (initialized_binding_value(initialized)->tag != EXPR_IMPLICIT_ANY && (ty_expr_type(me, initialized_binding_value(initialized))->flags & TYF_NULLABLE)) {
                any_parent = false;
            }
        }
        ret = any_parent ? object : ty_tuple_rest_slice(me, base, binding->parent_index);
        if (!ret && ty_is_tuple_target_empty(base)) ret = base;
        if (!ret) ret = ty_is_array(me, object) && object->reference.target != ty_readonly_array_target(me) ? object :
            ty_array(me, ty_iterated_type(me, ITERATION_DESTRUCTURING, object, span_null, nullptr), false);
    } else if (binding->rest) {
        // (What can't be spread, reported by binding_typecheck(), is the error type: tsc's getBindingElementTypeFromParentType)
        ret = ty_object_rest_source_valid(me, object) ? object_rest_type(me, object, pattern_keys(me, parent),
            binding->tag == BINDING_IDENTIFIER ? binding->symbol : nullptr) :
            ty_intrinsic(me, TY_ERROR);
    } else if (in_array) {
        Value index = value_number(binding->parent_index);
        // (Read like an expression: noUncheckedIndexedAccess's undefined, tsc's AccessFlags.ExpressionPosition)
        // (Of a generic tuple past its fixed elements, deferred like an element access: `[string, ...T][1]`, tsc's
        // getIndexedAccessTypeOrUndefined with the binding name as access node)
        ret = ty_is_tuple(object) && ty_is_generic_tuple(me, object) ?
            ty_indexed_include_undefined(me, object, ty_literal(me, &index), compiler_options()->no_unchecked_indexed_access) :
            ty_access_reading(me, object, ty_literal(me, &index));
        // Past the end of a tuple (TS2493, binding_typecheck()) is undefined (tsc's getIndexedAccessTypeOrUndefined)
        // (Of every member of a union of tuples too)
        TyList parts = object->kind == TY_UNION ? object->composite.types : (TyList){ .data = &object, .len = 1 };
        bool past_end = parts.len > 0;
        for (u32 p = 0; p < parts.len && past_end; p++) {
            TupleShape const* shape = ty_tuple_shape(parts.data[p]);
            bool has_rest = false;
            for (u32 i = 0; shape && i < shape->arity; i++) has_rest |= (shape->flags[i] & TUPLE_ELEMENT_REST) != 0;
            past_end = shape && !has_rest && binding->parent_index >= shape->arity;
        }
        if (past_end) ret = ty_intrinsic(me, TY_UNDEFINED);
        if (ret && !parent->has_explicit_type) ret = destructured_flow_type(me, binding, ret);
    } else if (compiler_options()->strict_null_checks && !(object->flags & (TYF_ANY | TYF_TYPE_PARAM)) &&
        ty_can_be_nullish(me, object) && pattern_parent_keeps_nullish(me, parent)
    ) {
        // `const { s } = t` with t possibly undefined: the property of the whole type, which undefined lacks (TS2339 by
        // binding_typecheck(), tsc's getBindingElementTypeFromParentType keeps undefined when the initializer has it)
        ret = ty_intrinsic(me, TY_ERROR);
    } else if (binding->parent_item->private_name) {
        ret = ty_intrinsic(me, TY_NEVER); // (A never key: getIndexedAccessType's never)
    } else {
        ObjectBindingItem const* item = binding->parent_item;
        Ty const* key = item->computed_key ? ty_regular(me, ty_binding_computed_key_type(me, item)) : ty_literal(me, &item->source);
        if (item->computed_key) {
            TY_WORKER_STATE(me);
            Ty const** used = (Ty const**)intern_slot(me, &my links, (u64[]){ KEY_BINDING_ACCESS_KEY, (u64)(uintptr_t)item }, 2);
            if (!*used) *used = key;
        }
        // (A generic key is a deferred `T[K]`: tsc's getIndexedAccessType)
        ret = item->computed_key && ty_is_generic_index(key) ? ty_indexed(me, object, key) : ty_access_reading(me, object, key);
        if (!ret && item->computed_key && (key->flags & TYF_ES_SYMBOL_LIKE)) {
            // A symbol key falls back to a string index signature (TS2538, tsc's getPropertyTypeForIndexType)
            Ty const* apparent = ty_apparent_type(me, object);
            IndexInfo const* index = (apparent->flags & TYF_OBJECT) ? ty_applicable_index_info(me, apparent, ty_intrinsic(me, TY_STRING)) : nullptr;
            if (index) ret = index->value;
        }
        // A property an object literal lacks is undefined when there's a default, which decides (tsc's
        // AccessFlags.AllowMissing)
        if (!ret && binding->default_value && object->kind == TY_ANONYMOUS && object->anonymous.object_literal) {
            ret = ty_intrinsic(me, TY_UNDEFINED);
        }
        // `{ 3: d }` of a tuple too short: undefined (tsc's getPropertyTypeForIndexType for a numeric name, TS2493)
        if (!ret && !item->computed_key) {
            bool is_number = false;
            Atom const* key_name = ty_property_name_of_key(me, ty_literal(me, &item->source), &is_number);
            csview name = key_name ? atom_sv(key_name) : c_sv("");
            bool digits = name.size > 0 && name.size < 10 && (name.size == 1 || name.buf[0] != '0');
            for (isize_t k = 0; digits && k < name.size; k++) digits = name.buf[k] >= '0' && name.buf[k] <= '9';
            TupleShape const* shape = digits ? ty_tuple_shape(object) : nullptr;
            bool has_rest = false;
            for (u32 k = 0; shape && k < shape->arity; k++) has_rest |= (shape->flags[k] & TUPLE_ELEMENT_REST) != 0;
            if (shape && !has_rest) ret = ty_intrinsic(me, TY_UNDEFINED);
        }
        if (ret && !parent->has_explicit_type) ret = destructured_flow_type(me, binding, ret);
        // A property the object lacks (TS2339, binding_typecheck()) is the error type (tsc's getIndexedAccessType
        // reporting it): a later `var` of the same name doesn't have to agree with it
        if (!ret) ret = ty_intrinsic(me, TY_ERROR);
    }
    return ret;
}


// An unannotated parameter of a function expression asked for before anything checked the function: its context
// root is checked first, which gives the parameter its contextual type (tsc's getContextuallyTypedParameterType
// resolves the call on demand, see ty_settle_function_context()). Not while the parameter is being evaluated
static void settle_parameter_context(Checker* me, InitializedBinding const* parameter) {
    FunctionImpl const* function = initialized_binding_rare(parameter)->context_function;
    if (!function || ty_contextual_parameter_type(me, parameter)) return;
    LazyMemo* own = lazy_type_state_peek((LazyType*)&parameter->lazy_type);
    if (own && own->evaluation_started) return;
    if (function == my annotated_context_function && my annotated_context_signature) {
        // Read by the function's own annotations while a call infers from them: the contextual signature they're
        // lowered against types it, as it stays (tsc's getTypeOfSymbol caches what getContextuallyTypedParameterType
        // gives then)
        u32 at = 0;
        for_each (i, vec_function_arg, &function->args) {
            if (&i->initializer == parameter) {
                Ty const* type = i->rest ? nullptr : ty_signature_param_type_at(me, my annotated_context_signature, at);
                if (type) ty_link_set(me, KEY_CONTEXT_PARAM, parameter, type);
                return;
            }
            bool is_this = i->initializer.binding.tag == BINDING_IDENTIFIER && span_equals(i->initializer.binding.content_span, "this");
            if (!is_this) at++;
        }
        return;
    }
    ty_settle_function_context(me, function);
}

static Ty const* binding_assigned_type(Checker* me, Binding const* binding) {
    Binding const* parent = binding->parent;
    // A catch variable (the only root without a source) is any, or unknown under useUnknownInCatchVariables
    if (!parent && !binding->source) {
        return ty_intrinsic(me, compiler_options()->use_unknown_in_catch_variables ? TY_UNKNOWN_ : TY_ANY);
    }
    if (!parent) {
        // (A for-of's pattern destructures the iterated type as it is, the variable is widened)
        if (binding->tag != BINDING_IDENTIFIER && binding->source->tag == LAZY_TYPE_FOR_EACH_LOOP_VAR) {
            return for_each_loop_statement_pattern_source_type(
                c_container_of(binding->source, ForEachLoopStatement, var_type));
        }
        // A JavaScript `var { a } = {}` destructures tsc's getJSContainerObjectType of the empty literal: an empty
        // JS-literal object, not an object literal (a missing property with a default is TS2339 unless open-ended)
        if (binding->tag != BINDING_IDENTIFIER && !binding->has_explicit_type && !binding_is_parameter(binding) &&
            binding->source->tag == LAZY_TYPE_INITIALIZED_BINDING && span_source(binding->content_span) &&
            span_source(binding->content_span)->is_js
        ) {
            Expr const* value = initialized_binding_value(c_container_of(binding->source, InitializedBinding, lazy_type));
            if (value->tag == EXPR_OBJECT && !vec_object_expr_item_size(&value->object)) {
                (void)lazy_type_get(binding->source); // (The initializer is still checked)
                return ty_object_literal(me, nullptr, 0, nullptr, 0, OBJECT_SYNTHETIC | OBJECT_JS);
            }
        }
        if (binding->source->tag == LAZY_TYPE_INITIALIZED_BINDING) {
            settle_parameter_context(me, c_container_of(binding->source, InitializedBinding, lazy_type));
        }
        return lazy_type_get(binding->source);
    }
    Ty const* object = ty_binding_type(me, parent, (LowerCtx){ .generic = true });
    // An ambient parameter's pattern takes no null or undefined: it's just documentation (tsc's
    // getBindingElementTypeFromParentType)
    if (compiler_options()->strict_null_checks && parent->ambient && binding_is_parameter(parent)) {
        object = ty_non_nullable(me, object);
    }
    // Under a written type, an initializer that can't be undefined takes undefined out of what's destructured (tsc's
    // getBindingElementTypeFromParentType, which looks at the initializer for that: `[a = a]: T = a` is circular)
    if (!parent->parent && parent->has_explicit_type && compiler_options()->strict_null_checks && parent->source &&
        parent->source->tag == LAZY_TYPE_INITIALIZED_BINDING && !(object->flags & TYF_ANY)
    ) {
        Expr const* value = initialized_binding_value(c_container_of(parent->source, InitializedBinding, lazy_type));
        if (value->tag != EXPR_IMPLICIT_ANY && !ty_can_equal_undefined(me, ty_expr_type(me, value))) {
            object = ty_without_undefined(me, object);
        }
    }
    // A rest element takes what the initializer is without the pattern as its contextual type (tsc's
    // CheckMode.RestBindingElement)
    if (binding->rest && !parent->parent && !parent->has_explicit_type && parent->source &&
        parent->source->tag == LAZY_TYPE_INITIALIZED_BINDING
    ) {
        InitializedBinding const* initialized = c_container_of(parent->source, InitializedBinding, lazy_type);
        Ty const* rest_object = ty_rest_binding_initializer_type(me, initialized_binding_value(initialized));
        if (rest_object) object = rest_object;
    }
    // Of any, any: the default isn't looked at (tsc's getBindingElementTypeFromParentType returns the parent type
    // first, so `{ f = f } = x as any` isn't circular). Not of the any a `= null` initializer widens to, whose
    // unwidened null tsc goes by
    if ((object->flags & TYF_ANY) && !binding->rest) {
        bool widened_null = false;
        if (!parent->parent && parent->source && parent->source->tag == LAZY_TYPE_INITIALIZED_BINDING) {
            InitializedBinding const* initialized = c_container_of(parent->source, InitializedBinding, lazy_type);
            widened_null = initialized_binding_value(initialized)->tag != EXPR_IMPLICIT_ANY &&
                (ty_expr_type(me, initialized_binding_value(initialized))->flags & TYF_NULLABLE);
        }
        // (A pattern whose own type is circular gives its names plain any: tsc's reportCircularityError returns anyType,
        // and only the declarations on the cycle, which the resolution marks, are reported. `nodes` of `const { pageInfo,
        // nodes } = data...` with data circular through pageInfo isn't, crowd-dev)
        if (!widened_null) return object == ty_intrinsic(me, TY_CIRCULAR) ? ty_intrinsic(me, TY_ANY) : object;
    }
    Ty const* ret = binding_element_of(me, binding, object);
    if (!ret) ret = ty_intrinsic(me, TY_ANY);
    // (An element of an unannotated pattern is widened itself: the pattern's source isn't, see initialized_binding's.
    // With a default, after undefined is replaced by it: `let [y = true] = [undefined]` is boolean without
    // strictNullChecks, not any)
    // (Only a variable is: a nested pattern destructures its element as it is, object literals and all, tsc's
    // getTypeForBindingElementParent. `[{ flag } = {}]` reads `flag` of `{ flag: X } | {}` as an object literal's)
    bool widens = !parent->has_explicit_type && !binding_is_parameter(parent) && binding->tag == BINDING_IDENTIFIER;
    Ty const* raw = ret;
    if (widens) ret = ty_widened_for_declaration(me, ret, false);
    if (widens && !binding->default_value && binding->tag == BINDING_IDENTIFIER && (raw->xflags & TYX_CONTAINS_WIDENING) &&
        !compiler_options()->strict_null_checks
    ) {
        // Its null and undefined widen to any (tsc's reportErrorsFromWidening of the element, TS7031)
        TY_WORKER_STATE(me);
        *intern_slot(me, &my links, (u64[]){ KEY_ELEMENT_WIDENED_ANY, (u64)(uintptr_t)binding }, 2) = raw;
    }
    if (binding->default_value && widens && !(raw->flags & TYF_ANY)) {
        Ty const* stripped = non_undefined_type(me, raw);
        if (stripped->flags & TYF_NEVER) ret = stripped;
    }
    if (binding->default_value) {
        // `[x = 1]`: undefined is replaced by the default. Under a written type, the default only removes undefined
        // (unless it can be undefined itself), else the two are a subtype-reduced union (tsc's getTypeForBindingElement)
        Binding const* root = parent;
        while (root->parent) root = root->parent;
        Ty const* types[2] = { non_undefined_type(me, ret), nullptr };
        if (root->has_explicit_type) {
            Ty const* default_type = ty_pad_initializer_type(me, ty_binding_default_type(me, binding), binding);
            // (any and unknown can't be undefined either: tsc's UnknownFacts lack IsUndefined; a type parameter can
            // through its constraint: `{ x = true as T }` of `T extends boolean | undefined` keeps `T | undefined`)
            if (compiler_options()->strict_null_checks && !ty_can_be_undefined(me, default_type)) {
                ret = types[0];
            }
            return ret;
        }
        // Not ty_check_expr() with types[0] as the contextual type: only the uncontextual path detects `[x = x]`
        Ty const* default_type = ty_pad_initializer_type(me, ty_binding_default_type(me, binding), binding);
        // tsc unions first and then widens what's left fresh: a default that fits (`name = "a"` for a `"a" | "b"`
        // property) is absorbed, literal and all
        // (Of mutual subtypes, two function types alike, the reduction below keeps the first in union order)
        if (!(ret->flags & TYF_ANY) && !(types[0]->flags & TYF_NEVER) &&
            ty_is_related(me, ty_regular(me, default_type), types[0], TY_RELATION_STRICT_SUBTYPE) &&
            // (A default object literal that is a mutual subtype is created after the element's type, so tsc's
            // removeSubtypes looks at it first and drops it: `{ p = {} }` of `P & object` keeps `P & object`)
            !((default_type->flags & TYF_OBJECT) && !(default_type->kind == TY_ANONYMOUS && default_type->anonymous.object_literal) &&
                ty_is_related(me, types[0], ty_regular(me, default_type), TY_RELATION_STRICT_SUBTYPE))
        ) {
            return types[0];
        }
        // (tsc's widenTypeInferredFromInitializer: a `const` keeps the literals)
        types[1] = binding->tag == BINDING_IDENTIFIER ?
            ty_widened_for_declaration(me, default_type, !binding_in_const_declaration(binding)) :
            binding_in_const_declaration(binding) ? default_type : ty_widened_literal(me, default_type);
        if (widens && binding->tag == BINDING_IDENTIFIER && !compiler_options()->strict_null_checks &&
            compiler_options()->no_implicit_any && ((raw->xflags | default_type->xflags) & TYX_CONTAINS_WIDENING)
        ) {
            // tsc widens the union of the element and its default as a whole: `[a = undefined] = []` is undefined
            // before that, which widens to any (reportErrorsFromWidening of the element, TS7031)
            Ty const* unwidened[2] = { non_undefined_type(me, raw), default_type };
            Ty const* pre = ty_union_subtype_reduced(me, unwidened, 2);
            if (pre->xflags & TYX_CONTAINS_WIDENING) {
                TY_WORKER_STATE(me);
                *intern_slot(me, &my links, (u64[]){ KEY_ELEMENT_WIDENED_ANY, (u64)(uintptr_t)binding }, 2) = pre;
            }
        }
        if (!(ret->flags & TYF_ANY)) {
            // What the reduction keeps is decided with the default still fresh (tsc unions first and widens after): a
            // fresh `{}` is no strict supertype of `{ flag: X }`, so `[{ flag } = {}]` keeps both (an object literal
            // inside an array literal default too: `[b, { x }] = ["s", { y: false }]` keeps the parent's `x`)
            Ty const* fresh[2] = { types[0], default_type };
            Ty const* reduced = ty_union_subtype_reduced(me, fresh, 2);
            TyList members = reduced->kind == TY_UNION ? reduced->composite.types : (TyList){ .data = &reduced, .len = 1 };
            // (A union default, `c ? { light: true } : undefined`, is flattened into the reduction: what is kept of
            // it is widened member by member. Widened first, its object literal would be no subtype of a type with
            // optional properties and stay)
            TyList defaults = default_type->kind == TY_UNION ? default_type->composite.types : (TyList){ .data = &default_type, .len = 1 };
            bool widened_default = false;
            TyBuf parts = {0};
            for (u32 i = 0; i < members.len; i++) {
                Ty const* m = members.data[i];
                bool from_default = m == default_type;
                for (u32 j = 0; j < defaults.len && !from_default; j++) from_default = m == defaults.data[j];
                if (from_default && m != types[0]) {
                    Ty const* widened = m == default_type ? types[1] : binding->tag == BINDING_IDENTIFIER ?
                        ty_widened_for_declaration(me, m, !binding_in_const_declaration(binding)) :
                        binding_in_const_declaration(binding) ? m : ty_widened_literal(me, m);
                    widened_default |= widened != m;
                    m = widened;
                }
                ty_buf_push(&parts, m);
            }
            // (The kept members, the default widened)
            ret = widened_default ? ty_union(me, parts.data, parts.len, nullptr, (TyList){0}) : reduced;
            ty_buf_drop(&parts);
        }
    }
    return ret;
}

// The type of a binding: what is written, else what is assigned. `ctx` is what a written type is lowered with
Ty const* ty_binding_type(Checker* me, Binding const* binding, LowerCtx ctx) {
    // A written type, or the name of an element of a pattern: resolved like tsc's getTypeOfVariableOrParameterOrProperty
    // pushing the symbol (a root pattern has none, see lazy_type.c), which reaching it again makes any (TS2502, TS7022).
    // Neither has a memo of its own, so a circular one is remembered
    if (!binding->has_explicit_type && !(binding->parent && binding->tag == BINDING_IDENTIFIER)) {
        return ty_binding_assigned_type(me, binding);
    }
    Ty const* circular = ty_intrinsic(me, TY_CIRCULAR);
    u64 key[2] = { KEY_CIRCULAR_BINDING, (u64)(uintptr_t)binding };
    if (my circular_bindings && intern_get(&my links, key, 2)) {
        TY_WORKER_STATE(me); // (A recorded cycle: this worker's)
        return circular;
    }
    if (ty_resolving_find(me, RESOLVING_BINDING, binding)) {
        ty_resolving_cycle(me, RESOLVING_BINDING, binding);
        return circular;
    }
    ty_resolving_push(me, RESOLVING_BINDING, binding);
    Ty const* type = binding->has_explicit_type ? ty_from_node(me, binding_type(binding), ctx) :
        ty_binding_assigned_type(me, binding);
    if (ty_resolving_pop(me)) {
        my circular_bindings = true;
        *intern_slot(me, &my links, key, 2) = (void*)binding;
        return circular;
    }
    // A catch variable annotated with anything but any or unknown is any (TS1196)
    if (binding->has_explicit_type && !binding->parent && !binding->source && !(type->flags & (TYF_ANY | TYF_UNKNOWN))) {
        return ty_intrinsic(me, TY_ANY);
    }
    return type;
}

static bool is_naked_type_param_node(TypeNode const* node) {
    if (node->tag != TYPE_IDENTIFIER || !node->identifier->target) return false;
    Symbol const* decl = node->identifier->target;
    return (decl->flags & SYMBOL_TYPE_PARAMETER) != 0;
}

// The `infer` type parameters a type node declares (what type_add_inferred_names() declares for an extends type)
static void collect_declared_infers(Checker* me, TypeNode const* node, TyBuf* out) {
    switch (node->tag) {
        case TYPE_IDENTIFIER:
            for_each (i, vec_spanned_type, &node->identifier->generic_params) collect_declared_infers(me, &i->type, out);
            break;
        case TYPE_INFERRED:
            if (node->inferred->owned_symbol) ty_buf_push(out, ty_type_param(me, node->inferred->owned_symbol));
            break;
        case TYPE_CALLABLE:
            for_each (i, vec_function_arg, &node->callable->function->args) {
                if (i->initializer.binding.has_explicit_type) collect_declared_infers(me, binding_type(&i->initializer.binding), out);
            }
            collect_declared_infers(me, &node->callable->function->ret, out);
            // (`(arg: any) => arg is infer R`)
            if (node->callable->function->predicate && node->callable->function->predicate->has_type) {
                collect_declared_infers(me, &node->callable->function->predicate->type, out);
            }
            break;
        case TYPE_INTERFACE:
            for_each (i, vec_interface_member, &node->interface.members) {
                if (i->key.tag == INTERFACE_KEY_MAPPED) {
                    collect_declared_infers(me, &i->key.mapped->in_type, out);
                    if (i->key.mapped->has_as_type) collect_declared_infers(me, &i->key.mapped->as_type, out);
                }
                collect_declared_infers(me, &i->type, out);
            }
            break;
        case TYPE_ARRAY: collect_declared_infers(me, node->array.element, out); break;
        case TYPE_JSDOC: collect_declared_infers(me, &node->jsdoc->type, out); break;
        case TYPE_PREDICATE: break;
        case TYPE_TUPLE:
            for_each (i, vec_tuple_element, &node->tuple.elements) collect_declared_infers(me, &i->type, out);
            break;
        case TYPE_INDEXED:
            collect_declared_infers(me, &node->indexed->interface, out);
            collect_declared_infers(me, &node->indexed->index, out);
            break;
        case TYPE_KEYOF: collect_declared_infers(me, &node->keyof->interface, out); break;
        case TYPE_TERNARY:
            // What its extends type declares is the nested conditional type's own
            collect_declared_infers(me, &node->ternary->maybe_sub, out);
            break;
        case TYPE_UNION:
        case TYPE_INTERSECTION:
            for_each (i, vec_type, &node->composite) collect_declared_infers(me, i, out);
            break;
        case TYPE_TEMPLATE:
            for_each (i, vec_type, &node->template_->types) collect_declared_infers(me, i, out);
            break;
        case TYPE_TYPEOF:
            if (node->typeof_->expr->tag == EXPR_GENERIC_PARAMS) {
                for_each (i, vec_spanned_type, &node->typeof_->expr->generic_params->params) collect_declared_infers(me, &i->type, out);
            }
            break;
        default: break;
    }
}

// tsc's isDeferredTypeReferenceNode for a reference to a generic interface or class: the body of an alias (which it then
// goes by), or a reference inside one with defaulted type arguments or ones that may resolve an alias, is a deferred
// reference whose type arguments are lowered on first use (`type RD<T> = ... ReadonlyArray<RD<I>>`). Null otherwise
static Ty const* deferred_interface_reference(
    Checker* me, TypeNode const* node, LowerCtx ctx, Symbol const* alias, TyList alias_args
) {
    IdentifierType const* identifier = node->identifier;
    if (identifier->keyword || identifier->jsdoc_intrinsic || span_valid(identifier_type_import_span(identifier))) return nullptr;
    Symbol const* decl = canonical(me, identifier->target);
    if (!decl || decl->is_placeholder || !(decl->flags & (SYMBOL_INTERFACE | SYMBOL_CLASS))) return nullptr;
    Ty const* declared = ty_declared_type_of(me, decl);
    if (declared->kind != TY_DECLARED || !declared->declared.type_params.len) return nullptr;
    TyList params = ty_local_type_params(declared);
    u32 written = (u32)vec_spanned_type_size(&identifier->generic_params);
    // A wrong number of type arguments is an error, not deferred
    if (written > params.len) return nullptr;
    // (Whether there are defaults is syntactic, tsc's hasTypeParameterDefault: lowering them could reach the alias this
    // is in, `type AST = Union | ...` with `class Union<M extends AST = AST>`)
    for (u32 i = written; i < params.len; i++) {
        if (!ty_param_has_default(params.data[i])) return nullptr;
    }
    if (!alias) {
        bool defer = written != params.len;
        for_each (i, vec_spanned_type, &identifier->generic_params) defer = defer || may_resolve_type_alias(me, &i->type);
        if (!defer) return nullptr;
    }
    return ty_deferred_reference(me, node, ctx, nullptr, alias, alias_args);
}

// Lowered when first needed, like tsc's (getTypeFromConditionalTypeNode only resolves the check and extends types)
static TyCondBranch lower_branch(TypeNode const* node, LowerCtx ctx) {
    return (TyCondBranch){ .node = node, .ctx = ctx };
}

// The check and extends types of a conditional type as they imply a constraint on the types in its true branch (tsc's
// getImpliedConstraint): `[A] extends [B]` implies what `A extends B` does
// (A node link memo: published when its making saw no event, which it can't. Its two types are shared words, plan 86 4f)
typedef struct ImpliedConstraint {
    TypeNode const* check_node;
    TypeNode const* extends_node;
    uint8_t check_state, extends_state; // In flight without --shared-graph (overlay.h)
    SharedWord check; // ty_actual_type_variable() of it
    SharedWord extends; // Lowered when first needed
} ImpliedConstraint;

static bool is_unary_tuple_node(TypeNode const* node) {
    if (node->tag != TYPE_TUPLE || vec_tuple_element_size(&node->tuple.elements) != 1) return false;
    TupleElement const* element = vec_tuple_element_at(&node->tuple.elements, 0);
    return !element->rest && !element->optional;
}

static ImpliedConstraint* implied_constraint(Checker* me, TernaryType const* ternary) {
    ImpliedConstraint* cached = (ImpliedConstraint*)ty_link_get_memo(me, KEY_IMPLIED_CONSTRAINT, ternary);
    if (cached) return cached;
    u64 start = graph_taint_now;
    ImpliedConstraint* ret = arena_new(&my arena, ImpliedConstraint);
    TypeNode const* check_node = &ternary->maybe_sub;
    TypeNode const* extends_node = &ternary->maybe_super;
    while (is_unary_tuple_node(check_node) && is_unary_tuple_node(extends_node)) {
        check_node = &vec_tuple_element_at(&check_node->tuple.elements, 0)->type;
        extends_node = &vec_tuple_element_at(&extends_node->tuple.elements, 0)->type;
    }
    ret->check_node = check_node;
    ret->extends_node = extends_node;
    ty_link_set_memo(me, KEY_IMPLIED_CONSTRAINT, ternary, ret, start);
    return ret;
}

// In the template of a homomorphic mapped type `{ [K in keyof T]: ... }` over a T constrained to arrays and tuples, K
// is also `number | `${number}`` (the other half of tsc's getConditionalFlowTypeOfType). Null if `ty` isn't that K
static Ty const* mapped_key_flow_constraint(Checker* me, InterfaceMember const* mapped, Ty const* ty, Ty const** actual) {
    if (ty->kind != TY_TYPE_PARAM || !mapped->key.mapped->owned_symbol || mapped->key.mapped->in_type.tag != TYPE_KEYOF) {
        return nullptr;
    }
    if (!*actual) *actual = ty_actual_type_variable(me, ty);
    if (*actual != ty_type_param(me, mapped->key.mapped->owned_symbol)) return nullptr;
    // (getHomomorphicTypeVariable: the constraint is `keyof T` for a type parameter T)
    Ty const* keys = ty_from_node(me, &mapped->key.mapped->in_type, (LowerCtx){ .generic = true });
    if (keys->kind != TY_KEYOF) return nullptr;
    Ty const* variable = ty_actual_type_variable(me, keys->keyof.operand);
    if (variable->kind != TY_TYPE_PARAM) return nullptr;
    Ty const* constraint = ty_param_constraint(me, variable);
    if (!constraint) return nullptr;
    TyList parts = constraint->kind == TY_UNION ? constraint->composite.types : (TyList){ .data = &constraint, .len = 1 };
    for (u32 i = 0; i < parts.len; i++) {
        if (!ty_is_tuple(parts.data[i]) && !ty_is_array(me, parts.data[i])) return nullptr;
    }
    Atom const* empty = checker_atom(me, csview_from(""));
    Atom const* texts[2] = { empty, empty };
    Ty const* number = ty_intrinsic(me, TY_NUMBER);
    Ty const* both[2] = { number, ty_template_literal(me, texts, &number, 1) };
    return ty_union(me, both, 2, nullptr, (TyList){0});
}

// tsc's getConditionalFlowTypeOfType: what the true branches around `node` imply about its type `ty`, as a
// substitution type. Only type variables are substituted in parameter positions (where the constraint could be wrong)
static Ty const* conditional_flow_type(Checker* me, TypeNode const* node, Ty const* ty) {
    Ty const* actual = nullptr;
    TyBuf constraints = {0};
    bool type_variable = ty->kind == TY_TYPE_PARAM || ty->kind == TY_INDEXED;
    for (TypeFlow const* flow = node->flow; flow && !flow->scope; flow = flow->outer) {
        if (flow->mapped) {
            Ty const* implied = mapped_key_flow_constraint(me, flow->mapped, ty, &actual);
            if (implied) ty_buf_push(&constraints, implied);
            continue;
        }
        if (!flow->covariant && !type_variable) continue;
        ImpliedConstraint* implied = implied_constraint(me, flow->ternary);
        uintptr_t v = sw_get(me, &implied->check);
        if (!v) {
            SwLook look = sw_look_fast(me, &implied->check, &implied->check_state, &v);
            if (look == SW_LOOK_MINE) continue; // (Its own cycle: a taint event)
            if (look == SW_LOOK_COMPUTE) {
                u64 taint = sw_claim_fast(me, &implied->check, &implied->check_state);
                Ty const* check = ty_actual_type_variable(me, ty_from_node(me, implied->check_node, (LowerCtx){ .generic = true }));
                SW_FP(TK_IMPLIED_CHECK, implied, check, taint);
                v = sw_finish_fast(me, &implied->check, &implied->check_state, (uintptr_t)check, taint);
            }
        }
        if (!actual) actual = ty_actual_type_variable(me, ty);
        if ((Ty const*)v != actual) continue;
        uintptr_t e = sw_get(me, &implied->extends);
        if (!e) {
            SwLook look = sw_look_fast(me, &implied->extends, &implied->extends_state, &e);
            if (look == SW_LOOK_MINE) continue; // (Its own cycle: a taint event)
            if (look == SW_LOOK_COMPUTE) {
                u64 taint = sw_claim_fast(me, &implied->extends, &implied->extends_state);
                Ty const* extends = ty_from_node(me, implied->extends_node, (LowerCtx){ .generic = true });
                e = sw_finish_fast(me, &implied->extends, &implied->extends_state, (uintptr_t)extends, taint);
            }
        }
        ty_buf_push(&constraints, (Ty const*)e);
    }
    if (constraints.len) {
        Ty const* constraint = constraints.len == 1 ? constraints.data[0] :
            ty_intersection(me, constraints.data, constraints.len, nullptr, (TyList){0});
        ty = ty_substitution(me, ty, constraint);
    }
    ty_buf_drop(&constraints);
    return ty;
}

static Ty const* lower_node(Checker* me, TypeNode const* node, LowerCtx outer_ctx, Symbol const* alias, TyList alias_args);

static inline __attribute__((always_inline)) Ty const* lower(
    Checker* me, TypeNode const* node, LowerCtx ctx, Symbol const* alias, TyList alias_args
) {
    // A union node or a generic reference outside an alias body is lowered once (a cache, tsc's links.resolvedType)
    bool memo_node = node->tag == TYPE_UNION ||
        (node->tag == TYPE_IDENTIFIER && vec_spanned_type_size(&node->identifier->generic_params));
    if (memo_node && !alias && !ctx.in_alias && !ctx.method && !type_flow_in_branch(node->flow)) {
        u64 key[2] = { KEY_UNION_NODE | (u64)ctx.generic << 32, (u64)(uintptr_t)node };
        Ty const* memo = intern_get(&my links, key, 2);
        if (memo) return memo;
        Ty const* ret = lower_node(me, node, ctx, alias, alias_args);
        *intern_slot(me, &my links, key, 2) = ret;
        return ret;
    }
    // A template whose cross product is too large (TS2590 at the node being checked) is the error type however it is
    // lowered again: reported once per node, like tsc's links.resolvedType
    if (node->tag == TYPE_TEMPLATE) {
        u64 key[2] = { KEY_UNION_NODE | (u64)1 << 40, (u64)(uintptr_t)node };
        if (intern_get(&my links, key, 2)) return ty_intrinsic(me, TY_ERROR);
        Ty const* ret = lower_node(me, node, ctx, alias, alias_args);
        if (ret == ty_intrinsic(me, TY_ERROR)) *intern_slot(me, &my links, key, 2) = ret;
        return type_flow_in_branch(node->flow) ? conditional_flow_type(me, node, ret) : ret;
    }
    Ty const* ret = lower_node(me, node, ctx, alias, alias_args);
    return type_flow_in_branch(node->flow) ? conditional_flow_type(me, node, ret) : ret;
}

static Ty const* lower_node(Checker* me, TypeNode const* node, LowerCtx outer_ctx, Symbol const* alias, TyList alias_args) {
    // What the operands of this node are lowered with
    LowerCtx ctx = outer_ctx;
    ctx.method = false;
    switch (node->tag) {
        case TYPE_ANY: return ty_intrinsic(me, node->any.self_referential_error ? TY_ERROR : TY_ANY);
        case TYPE_UNKNOWN: return ty_intrinsic(me, TY_UNKNOWN_);
        case TYPE_NEVER: return ty_intrinsic(me, TY_NEVER);
        case TYPE_VOID: return ty_intrinsic(me, TY_VOID);
        case TYPE_BOOLEAN: return ty_intrinsic(me, TY_BOOLEAN);
        case TYPE_NUMBER: return ty_intrinsic(me, TY_NUMBER);
        case TYPE_STRING: return ty_intrinsic(me, TY_STRING);
        case TYPE_SYMBOL: return ty_intrinsic(me, TY_ES_SYMBOL);
        case TYPE_UNIQUE_SYMBOL: return ty_unique_symbol(me, &node->unique_symbol);
        case TYPE_BIGINT: return ty_intrinsic(me, TY_BIGINT);
        case TYPE_VALUE: return ty_literal(me, &node->value);
        case TYPE_IDENTIFIER: {
            if (alias || ctx.in_alias) {
                Ty const* deferred = deferred_interface_reference(me, node, ctx, alias, alias_args);
                if (deferred) return deferred;
            }
            // tsc's walk checks a type reference (checkSourceElement: its errors go there) before anything asks for its
            // type; tttt's often asks first. A generic reference inside the node being checked stands for it
            CurrentNode saved_node = my current_node;
            if (vec_spanned_type_size(&node->identifier->generic_params) &&
                span_contains_span(my current_node.expr ? ty_current_node_span(me) : my current_node.span, node->identifier->name)
            ) {
                my current_node = (CurrentNode){ .span = identifier_type_span(node->identifier) };
            }
            Ty const* ret = lower_identifier(me, node->identifier, ctx, alias);
            my current_node = saved_node;
            // (Under a conditional type distributing over it: the distributed twin, plan 57)
            if (node->identifier->distributed && ret->kind == TY_TYPE_PARAM) ret = ty_distributed_type_param(me, ret);
            Symbol const* decl = node->identifier->target;
            // The body of an alias is an instantiation of a generic alias: it goes by the new name
            // Not when a conditional type resolved (`type C = Chars<"ab">` is the union made along the way, tsc's
            // getConditionalType only names a deferred one or a distribution over a union), nor for an intrinsic (`type H = Uppercase<"a" | "b">`)
            // (Syntactically: the alias may be being resolved)
            TypeNode const* body = alias && decl && symbol_is_generic_alias(decl) ? alias_body_node(canonical(me, decl)) : nullptr;
            bool renames = false;
            bool distributed = false; // (Renamed as a distribution's union)
            bool homomorphic_object = false; // (Keeps its name, see ty_alias_body_mapped_instance())
            if (body && !is_intrinsic_body(body)) {
                // An object type takes the new name when instantiated (tsc's getObjectTypeInstantiation), except a
                // homomorphic mapped type; anything else keeps a name only if it still is what the alias names
                SymbolLinks const* links = symbol_links(me, canonical(me, decl));
                uintptr_t declared_v = sw_peek_fast(me, &links->declared); // (A done-ness peek)
                Ty const* declared = declared_v ? DECLARED_TYPE(declared_v) : nullptr;
                bool object = declared ? declared->kind == TY_ANONYMOUS : body->tag == TYPE_INTERFACE || body->tag == TYPE_CALLABLE;
                // (The declared type's node when there is one: `{ [K in keyof T]: T[K] } & {}` is the mapped type;
                // `type GP<T> = Partial<T>` declares an instantiation of Partial's, which is homomorphic too)
                Ty const* declared_object = declared && declared->kind == TY_ANONYMOUS ? declared : nullptr;
                if (declared_object && !declared_object->anonymous.node && declared_object->anonymous.target) {
                    declared_object = declared_object->anonymous.target;
                }
                TypeNode const* object_node = declared_object && declared_object->kind == TY_ANONYMOUS &&
                    declared_object->anonymous.node ? declared_object->anonymous.node : body;
                // (A union is instantiated under the new name whatever name it had: tsc's instantiateTypeWorker passes the
                // alias to getUnionType, `type T05 = T02<A>` with `T02<T> = keyof (T & B)`, the union `keyof T | "b"`)
                bool union_body = declared && declared->kind == TY_UNION &&
                    ret->kind == TY_UNION;
                // (Homomorphic as tsc's getHomomorphicTypeVariable sees the lowered constraint when it's known:
                // `[K in keyof Helper<T>]` with a mapped Helper is `keyof T`)
                bool homomorphic = declared_object && declared_object->anonymous.node == object_node ?
                    ty_mapped_homomorphic_variable(me, declared_object) != nullptr : is_homomorphic_mapped_node(object_node);
                renames = object ? !homomorphic || ret->kind == TY_UNION :
                    ret->alias == canonical(me, decl) || union_body;
                homomorphic_object = object && homomorphic && ret->kind == TY_ANONYMOUS;
                // An indexed access distributed over a union index is that union by the new name (getIndexedAccessType),
                // and so is a conditional type distributed over a union (getConditionalTypeInstantiation's mapTypeWithAlias)
                Ty const* distributes_over = !declared ? nullptr :
                    declared->kind == TY_INDEXED ? declared->indexed.index :
                    declared->kind == TY_CONDITIONAL && declared->conditional.distributive ? declared->conditional.check : nullptr;
                // (A deferred instance of the conditional type it declares, which instantiation names by the
                // conditional type's root alias: tsc's getConditionalTypeInstantiation takes the alias given)
                if (!renames && declared && declared->kind == TY_CONDITIONAL && ret->kind == TY_CONDITIONAL &&
                    ret->conditional.root == declared->conditional.root
                ) {
                    renames = true;
                }
                if (!renames && ret->kind == TY_UNION && distributes_over) {
                    Ty const* args_buf[MAX_TYPE_ARGS];
                    TyList params = ty_type_params_of(me, symbol_type_generic(canonical(me, decl)));
                    TyList args = lower_type_args(me, params, 0, &node->identifier->generic_params, ctx, args_buf, false);
                    TyMapper const* mapper = ty_mapper_new(me, params, args);
                    renames = distributed = ty_instantiate(me, distributes_over, mapper)->kind == TY_UNION;
                }
            }
            // A local alias doesn't rename an instantiation of a top-level one (tsc's getTypeFromTypeAliasReference: it
            // may end up in an inferred return type where it isn't accessible, TypeScript#43622)
            if (alias_is_local(alias) && !alias_is_local(canonical(me, decl))) renames = homomorphic_object = false;
            if (homomorphic_object) ret = ty_alias_body_mapped_instance(me, ret, alias, alias_args);
            if (renames && distributed && ret->kind == TY_UNION && (ret->flags & TYF_BOOLEAN) && ret->alias != alias) {
                // (`true | false` made by the distribution too, which ty_realias() leaves alone for `type B<T> =
                // boolean`: tsc's mapTypeWithAlias makes a union of its own by the alias, `type N3 = Not<boolean>`)
                ret = ty_union(me, ret->composite.types.data, ret->composite.types.len, alias, alias_args);
            } else if (renames) {
                ret = ty_realias(me, ret, alias, alias_args);
            }
            return ret;
        }

        case TYPE_INFERRED: {
            // (Outside a conditional's extends clause, TS1338: an anonymous type parameter, tsc's
            // bindAnonymousDeclaration)
            if (!node->inferred->owned_symbol) {
                // (Its implied constraint all the same, tsc's getInferredTypeParameterConstraint goes by where the
                // declaration is: `${infer X}` is a string)
                Ty const* param = ty_synthetic_type_param(me, span_sv(node->inferred->name));
                InferPosition position = node->inferred->position;
                Ty const* constraint = position == INFER_POSITION_TEMPLATE ? ty_intrinsic(me, TY_STRING) :
                    position == INFER_POSITION_REST ? ty_array(me, ty_intrinsic(me, TY_UNKNOWN_), false) :
                    position == INFER_POSITION_MAPPED_KEY ? ty_keyof(me, ty_intrinsic(me, TY_ANY)) : nullptr;
                if (constraint) {
                    ty_param_constraint_init(param->param.state, constraint, false);
                }
                return param;
            }
            Ty const* param = ty_type_param(me, node->inferred->owned_symbol);
            if (!param->param.state->is_infer) __atomic_store_n(&param->param.state->is_infer, true, __ATOMIC_RELAXED);
            return param;
        }

        case TYPE_INTERFACE:
            // (`{}` without an alias is one type, tsc's emptyTypeLiteralType: its id is older than anything written)
            if (!alias && !vec_interface_member_size(&node->interface.members) && !node->interface.jsdoc_literal) {
                return ty_empty_type_literal(me);
            }
            // tsc's getTypeFromMappedTypeNode resolves the key's constraint eagerly (its circularity is found under
            // whatever is being resolved, an alias's declaration for one)
            if (vec_interface_member_size(&node->interface.members) == 1) {
                InterfaceMember const* member = vec_interface_member_at(&node->interface.members, 0);
                if (member->key.tag == INTERFACE_KEY_MAPPED && member->key.mapped->owned_symbol) {
                    Ty const* ret = ty_anonymous_from_node(me, node, ctx, alias, alias_args);
                    ty_param_constraint_is_circular(me, ty_type_param(me, member->key.mapped->owned_symbol));
                    return ret;
                }
            }
            return ty_anonymous_from_node(me, node, ctx, alias, alias_args);

        case TYPE_CALLABLE:
            return ty_anonymous_from_node(me, node, outer_ctx, alias, alias_args);

        case TYPE_JSDOC: return lower(me, &node->jsdoc->type, outer_ctx, alias, alias_args);
        case TYPE_PREDICATE: return ty_intrinsic(me, node->predicate->asserts ? TY_VOID : TY_BOOLEAN);

        case TYPE_ARRAY:
            // tsc's isDeferredTypeReferenceNode
            if (alias || (ctx.in_alias && may_resolve_type_alias(me, node->array.element))) {
                return ty_deferred_reference(me, node, ctx, nullptr, alias, alias_args);
            }
        {
            Ty const* element = ty_from_node(me, node->array.element, ctx);
            bool outer = my creating_from_type_node;
            my creating_from_type_node = true;
            Ty const* ret = ty_array(me, element, node->array.readonly);
            my creating_from_type_node = outer;
            return ret;
        }

        case TYPE_TUPLE: {
            // A tuple with a variadic element (going by the syntax) isn't deferred in tsc, so it goes by no alias
            // An empty tuple is its target (tsc's isDeferredTypeReferenceNode, getTypeFromArrayOrTupleTypeNode)
            if (!tuple_node_has_variadic_member(node) && vec_tuple_element_size(&node->tuple.elements) &&
                (alias || (ctx.in_alias && tuple_elements_may_resolve_type_alias(me, node)))
            ) {
                return ty_deferred_reference(me, node, ctx, nullptr, alias, alias_args);
            }
        }
            return lower_tuple(me, node, ctx);

        case TYPE_INDEXED: {
            Ty const* index = ty_from_node(me, &node->indexed->index, ctx);
            Ty const* object = ty_from_node(me, &node->indexed->interface, ctx);
            Ty const* ret = ty_indexed_written(me, object, index);
            // A key the object lacks is reported right away, the object printed into the message (tsc's
            // getIndexedAccessType with its accessNode): printing resolves an anonymous object's member types, so
            // `type A = { a: A }["x"]` is circular like `{ a: A }["a"]` (the TS2339 itself is type_typecheck()'s)
            if (ret == ty_intrinsic(me, TY_ERROR) && object->kind == TY_ANONYMOUS) {
                cstr printed = ty_to_cstr(me, object);
                cstr_drop(&printed);
            }
            // `T["a"]` written as a type reads an optional property's missing as a real undefined (tsc's
            // getPropertyTypeForIndexType with an IndexedAccessTypeNode)
            if (ty_contains_missing(me, ret)) ret = ty_union(me, (Ty const*[]){ ret, ty_intrinsic(me, TY_UNDEFINED) }, 2, nullptr, (TyList){0});
            // With a union index the result is the union of what each member accesses, made under the alias's name
            // (tsc's getIndexedAccessType passes the alias symbol to getUnionType)
            // (An indexed access this one made, not one it read out of a property: `{ a: X[K] }["a"]` is X[K] as it is,
            // tsc's getIndexedAccessType only names the IndexedAccessType it creates for a generic object or index)
            bool renames = (ret->kind == TY_INDEXED && ret->indexed.object == object && ret->indexed.index == index) ||
                (ret->kind == TY_UNION && index->kind == TY_UNION);
            // (boolean too, which ty_realias() leaves alone for `type B = boolean`)
            if (alias && ret->kind == TY_UNION && index->kind == TY_UNION && (ret->flags & TYF_BOOLEAN) && ret->alias != alias) {
                return ty_union(me, ret->composite.types.data, ret->composite.types.len, alias, alias_args);
            }
            return alias && renames ? ty_realias(me, ret, alias, alias_args) : ret;
        }

        case TYPE_KEYOF:
            return ty_keyof(me, ty_from_node(me, &node->keyof->interface, ctx));

        case TYPE_TYPEOF: {
            // Widened like tsc's getTypeFromTypeQueryNode: `typeof undefined` is any without strictNullChecks.
            // (Narrowed where the query is, like any reference: tsc's checkIdentifier)
            Ty const* ret = ty_regular(me, ty_widened_for_declaration(me, ty_expr_type(me, node->typeof_->expr), false)); // (objects too)
            // `typeof f<T>`: the printer reuses the node for that very type (tsc's InstantiationExpressionType.node)
            // and each node has a type of its own (tsc's getInstantiationExpressionType caches per node): never the
            // interned one the expression `f<T>` itself has, which prints structurally
            // (Only a query with type arguments of its own: `typeof f0` of `const f0 = f<T>` is f0's type, whose
            // instantiation node is an expression, printed structurally)
            if (ret->kind == TY_ANONYMOUS && ret->anonymous.applies_type_args && node->typeof_->expr->tag == EXPR_GENERIC_PARAMS) {
                u64 node_key[3] = { KEY_INSTANTIATION_QUERY, (u64)(uintptr_t)node, ret->id };
                Ty const* own = intern_get(&my links, node_key, 3);
                if (own) return own;
                Ty* fresh = ty_alloc(me, TY_ANONYMOUS, ret->flags);
                fresh->anonymous.target = ret->anonymous.target;
                fresh->anonymous.mapper = ret->anonymous.mapper;
                fresh->anonymous.applies_type_args = true;
                fresh->anonymous.free_params_unknown = true;
                fresh->anonymous.state = arena_new(&my arena, TyObjectState);
                // (The type -> node entry first: `fresh` is this node's alone, so its node is part of what it is, and
                // must be there before the node -> type entry publishes it. No first-lowerer history: one fresh type per
                // (node, instantiation), census A section 4's worry)
                intern_set(me, &my links, intern_slot(me, &my links, (u64[]){ KEY_INSTANTIATION_QUERY, fresh->id }, 2), node);
                ret = intern_set(me, &my links, intern_slot(me, &my links, node_key, 3), fresh);
            }
            return ret;
        }

        case TYPE_TERNARY: {
            // The extends type goes before the branches, it's what declares the `infer` type parameters
            LowerCtx inner = ctx;
            inner.generic = true;
            TypeNode const* extends_node = &node->ternary->maybe_super;
            Ty const* check_type = ty_from_node(me, &node->ternary->maybe_sub, ctx);
            Ty const* extends = ty_from_node(me, &node->ternary->maybe_super, inner);
            TyCondBranch if_true = lower_branch(&node->ternary->if_true, inner);
            TyCondBranch if_false = lower_branch(&node->ternary->if_false, ctx);
            // (tsc's isDistributive looks at the check type: a type parameter substituted by an outer true branch
            // isn't one)
            // (A `this` check type is the class's or interface's this-type parameter: a type parameter, distributive)
            TypeNode const* check_node = &node->ternary->maybe_sub;
            bool this_check = check_node->tag == TYPE_IDENTIFIER && check_node->identifier->keyword == KEYWORD_IDENTIFIER_THIS;
            bool distributive = (is_naked_type_param_node(check_node) || this_check) && check_type->kind == TY_TYPE_PARAM;
            TyBuf infers = {0};
            collect_declared_infers(me, extends_node, &infers);
            // (tsc's checkTuples looks at the nodes: `T extends [infer A]` with T a tuple doesn't defer on its elements)
            bool check_tuples = node->ternary->maybe_sub.tag == TYPE_TUPLE && extends_node->tag == TYPE_TUPLE &&
                ty_conditional_simple_tuples(check_type, extends);
            // (The root's outer type parameters, and its own infers, which instantiation must leave alone whatever an
            // outer one of the same declaration inferred (`Split<B, D>` in the true branch of Split, see instantiate.c),
            // are made with it: ty_root_outer_of_node(). A root is only made here, keyed by the branches' nodes, so one
            // found again is this node's, with them)
            TyRootSpec spec = { .node = node, .alias_args = alias ? alias_args : (TyList){0} };
            Ty const* ret = ty_conditional(
                me, check_type, extends, &if_true, &if_false, nullptr, nullptr, distributive, check_tuples,
                (TyList){ .data = infers.data, .len = infers.len }, &spec
            );
            ty_buf_drop(&infers);
            // (Only the conditional type this node makes goes by the alias: a branch it resolved to that is another
            // conditional type is that one, tsc's getConditionalType passes the alias to the deferred type alone)
            if (!alias || ret->kind != TY_CONDITIONAL || ret->conditional.root != ret || ret->conditional.check != check_type ||
                ret->conditional.extends != extends
            ) {
                return ret;
            }
            Ty const* aliased = ty_realias(me, ret, alias, alias_args);
            // (The first alias to lower the root names it: a CAS, another worker's may have won)
            if (ret->conditional.root == ret && !ty_root_aliased(ret)) {
                Ty const* expected = nullptr;
                __atomic_compare_exchange_n(&((Ty*)ret)->conditional.root_aliased, &expected, aliased, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
            }
            return aliased;
        }

        case TYPE_UNION:
        case TYPE_INTERSECTION: {
            u32 len = (u32)vec_type_size(&node->composite);
            Ty const** types = malloc(sizeof(Ty const*) * (len ? len : 1));
            u32 placeholders = 0;
            for_each (i, vec_type, &node->composite) {
                // (parse_type() turns the leading operator of `& A & B` into an `any` operand, `never` for `|`)
                if (i->placeholder) {
                    placeholders++;
                    continue;
                }
                types[i_index - placeholders] = ty_from_node(me, i, ctx);
            }
            len -= placeholders;
            // A written `string & {}` (or number, bigint) stays as it is: the idiom for "any string, but show the
            // literals" (tsc's NoSupertypeReduction)
            // (Either way round, `{} & string`, and a pattern template literal too. By type, tsc's emptyTypeLiteralType:
            // `string & NonNullable<unknown>` is one too, NonNullable<unknown> is lib's `{}`, atproto's UnknownString)
            bool keep_empty_object = false;
            if (node->tag == TYPE_INTERSECTION && len == 2 && !placeholders) {
                for (u32 e = 0; e < 2 && !keep_empty_object; e++) {
                    Ty const* t = types[1 - e];
                    keep_empty_object = types[e] == ty_empty_type_literal(me) &&
                        (t == ty_intrinsic(me, TY_STRING) || t == ty_intrinsic(me, TY_NUMBER) || t == ty_intrinsic(me, TY_BIGINT) ||
                            (t->kind == TY_TEMPLATE_LITERAL && ty_is_pattern_literal(t)));
                }
            }
            Ty const* ret = node->tag == TYPE_UNION ? ty_union(me, types, len, alias, alias_args) :
                keep_empty_object ? ty_intersection_keeping_empty_object(me, types, len, alias, alias_args) :
                ty_intersection(me, types, len, alias, alias_args);
            free(types);
            return ret;
        }

        case TYPE_TEMPLATE: {
            TemplateType const* template = node->template_;
            u32 len = (u32)vec_type_size(&template->types);
            Ty const** types = malloc(sizeof(Ty const*) * len);
            Atom const** texts = malloc(sizeof(Atom const*) * (len + 1));
            panic_if(!types || !texts, "out of memory");
            ctx.in_alias = false; // tsc's isResolvedByTypeAlias stops at template literal types
            for_each (i, vec_type, &template->types) {
                types[i_index] = ty_from_node(me, i, ctx);
            }
            for_each (i, ast_cstrs, &template->texts) texts[i_index] = checker_atom(me, cstr_sv(i));
            Ty const* ret = ty_template_literal(me, texts, types, len);
            free(types);
            free(texts);
            return ret;
        }

    }
    impossible;
}

Ty const* ty_from_node(Checker* me, TypeNode const* node, LowerCtx ctx) {
    return lower(me, node, ctx, nullptr, (TyList){0});
}
