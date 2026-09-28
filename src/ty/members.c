#include "../source_file.h"
#include "ty/ty_internal.h"
#include "scope.h"
#include "class_decl.h"
#include "enum_decl.h"
#include "compiler_options.h"

// Members of object types are resolved on first use, never at construction. That's what makes recursive types legal:
// `interface List<T> { tail: List<T> }` only needs the *identity* of `List<T>` to describe its members.

static const Members empty_members = {0};

typedef struct MembersBuilder {
    Prop** props;
    u32 props_count, props_cap;
    Signature** call;
    u32 call_count, call_cap;
    Signature** construct;
    u32 construct_count, construct_cap;
    IndexInfo* index;
    u32 index_count, index_cap;
    MappedInfo const* mapped;
    TyDeclGroup const* decl_group; // The interface declaration being added (overload order, see reorder_candidates)
} MembersBuilder;

#define builder_push(me, field, value) do { \
        if ((me)->field##_count == (me)->field##_cap) { \
            (me)->field##_cap = (me)->field##_cap ? (me)->field##_cap * 2 : 8; \
            (me)->field = realloc((me)->field, sizeof(*(me)->field) * (me)->field##_cap); \
            panic_if(!(me)->field, "out of memory"); \
        } \
        (me)->field[(me)->field##_count++] = (value); \
    } while (false)

static Prop* builder_find_prop(MembersBuilder const* me, Atom const* name) {
    for (u32 i = 0; i < my props_count; i++) {
        if (my props[i]->name == name) return my props[i];
    }
    return nullptr;
}

static int prop_cmp_name(void const* a, void const* b) {
    uintptr_t a_name = (uintptr_t)(*(Prop* const*)a)->name;
    uintptr_t b_name = (uintptr_t)(*(Prop* const*)b)->name;
    return a_name < b_name ? -1 : a_name > b_name;
}

static Members const* builder_finish(Checker* me, MembersBuilder* builder) {
    Members* ret = arena_new(&my arena, Members);
    if (builder->props_count > 8) {
        Prop** sorted = arena_new_n(&my arena, Prop*, builder->props_count);
        memcpy(sorted, builder->props, sizeof(Prop*) * builder->props_count);
        qsort(sorted, builder->props_count, sizeof(Prop*), prop_cmp_name);
        ret->props_by_name = sorted;
    }
#define FINISH(field, count_field, T) \
    if (builder->field##_count) { \
        T* copy = arena_new_n(&my arena, T, builder->field##_count); \
        memcpy(copy, builder->field, sizeof(T) * builder->field##_count); \
        ret->field = copy; \
        ret->count_field = builder->field##_count; \
    } \
    free(builder->field);
    FINISH(props, prop_count, Prop*)
    FINISH(call, call_count, Signature*)
    FINISH(construct, construct_count, Signature*)
    FINISH(index, index_count, IndexInfo)
#undef FINISH
    ret->mapped = builder->mapped;
    return ret;
}

static Signature* signature_from_function(Checker* me, FunctionImpl const* function, LowerCtx ctx) {
    Signature* ret = arena_new(&my arena, Signature);
    ret->function = function;
    ret->ctx = ctx;
    if (function->is_constructor) ret->flags |= SIGNATURE_CONSTRUCT;
    if (function->is_abstract_constructor) ret->flags |= SIGNATURE_ABSTRACT;
    if (ctx.method) ret->flags |= SIGNATURE_METHOD;
    return ret;
}

static Signature* builder_add_callable(Checker* me, MembersBuilder* builder, FunctionImpl const* function, LowerCtx ctx) {
    Signature* sig = signature_from_function(me, function, ctx);
    if (sig->flags & SIGNATURE_CONSTRUCT) {
        builder_push(builder, construct, sig);
    } else {
        builder_push(builder, call, sig);
    }
    return sig;
}

static void prop_add_decl_spans(Checker* me, Prop* prop, Span const* spans, u32 count) {
    if (!count) return;
    Span* all = arena_new_n(&my arena, Span, prop->decl_span_count + count);
    if (prop->decl_span_count) memcpy(all, prop->decl_spans, sizeof(Span) * prop->decl_span_count);
    memcpy(all + prop->decl_span_count, spans, sizeof(Span) * count);
    prop->decl_spans = all;
    prop->decl_span_count += count;
}

// tsc's isThislessType: types that syntactically can't mention `this` (keywords, literals, arrays of those, names
// whose type arguments are those). Anything else is assumed to
static bool type_node_is_thisless(TypeNode const* node) {
    switch (node->tag) {
        case TYPE_ANY: case TYPE_UNKNOWN: case TYPE_NEVER: case TYPE_VOID: case TYPE_BOOLEAN: case TYPE_NUMBER:
        case TYPE_STRING: case TYPE_SYMBOL: case TYPE_BIGINT: case TYPE_VALUE: case TYPE_UNIQUE_SYMBOL:
            return true;
        case TYPE_ARRAY:
            return type_node_is_thisless(node->array.element);
        case TYPE_IDENTIFIER:
            if (node->identifier->keyword == KEYWORD_IDENTIFIER_THIS) return false;
            for_each (i, vec_spanned_type, &node->identifier->generic_params) {
                if (!type_node_is_thisless(&i->type)) return false;
            }
            return true;
        default:
            return false;
    }
}

// tsc's isThislessVariableLikeDeclaration: an annotation that is thisless, or neither an annotation nor an initializer
static bool binding_is_thisless(bool has_type, TypeNode const* type, bool has_initializer) {
    return has_type ? type_node_is_thisless(type) : !has_initializer;
}

// tsc's isThislessFunctionLikeDeclaration
static bool function_is_thisless(FunctionImpl const* function) {
    if (!function->is_constructor && !(function->ret_explicit && type_node_is_thisless(&function->ret))) return false;
    for_each (i, vec_function_arg, &function->args) {
        InitializedBinding const* binding = &i->initializer;
        if (!binding_is_thisless(binding->binding.has_explicit_type, &binding->binding.explicit_type,
                binding->value.tag != EXPR_IMPLICIT_ANY)) {
            return false;
        }
    }
    for_each (i, vec_generic_item, &function->generic.items) {
        if (i->extends_explicit && !type_node_is_thisless(&i->extends)) return false;
    }
    return true;
}

// Adds the members written in (or, for classes/enums/namespaces, evaluated by the legacy checker into) an Interface
static void set_decl_group(Checker* me, FunctionImpl const* function, TyDeclGroup const* group) {
    u64 key[2] = { KEY_DECL_GROUP, (u64)(uintptr_t)function };
    *intern_slot(me, &my links, key, 2) = group;
}

// The interface declaration a method or call signature was written in, null if it's not an interface's
TyDeclGroup const* ty_decl_group(Checker* me, FunctionImpl const* function) {
    u64 key[2] = { KEY_DECL_GROUP, (u64)(uintptr_t)function };
    return intern_get(&my links, key, 2);
}

static TyDeclGroup const* decl_group_of(Checker* me, Symbol const* symbol, SymbolDecl const* node) {
    u64 key[2] = { KEY_DECL_GROUP, (u64)(uintptr_t)node };
    void const** slot = intern_slot(me, &my links, key, 2);
    if (!*slot) {
        TyDeclGroup* group = arena_new(&my arena, TyDeclGroup);
        group->symbol = symbol;
        group->declaration = node;
        *slot = group;
    }
    return *slot;
}

static void builder_add_interface_member(
    Checker* me, MembersBuilder* builder, InterfaceMember const* i, LowerCtx ctx, Symbol const* parent
) {
    {
        switch (i->key.tag) {
            case INTERFACE_KEY_NAME:
            case INTERFACE_KEY_EXPR: {
                Atom const* name = nullptr;
                if (interface_member_is_private_name(i)) break;
                if (i->key.tag == INTERFACE_KEY_NAME) {
                    name = checker_atom(me, cstr_sv(&i->key.name_string));
                } else {
                    Ty const* key = ty_expr_type(me, i->key.expr.ref);
                    name = ty_literal_name(me, key);
                    if (!name) break;
                }
                bool is_method = i->key.tag == INTERFACE_KEY_NAME && (i->is_method || i->key.name_is_member_function) &&
                    i->type.tag == TYPE_CALLABLE;
                if (is_method && builder->decl_group) set_decl_group(me, i->type.callable->function, builder->decl_group);
                Prop* existing = builder_find_prop(builder, name);
                if (existing) {
                    // Overloads are separate members in the legacy interface and one method with several signatures here
                    if (is_method && (existing->flags & PROP_METHOD) && existing->node) {
                        u32 count = existing->overload_count ? existing->overload_count : 1;
                        FunctionImpl const** functions = arena_new_n(&my arena, FunctionImpl const*, count + 1);
                        if (existing->overload_count) {
                            memcpy(functions, existing->overloads, sizeof(FunctionImpl const*) * count);
                        } else {
                            functions[0] = existing->node->callable->function;
                        }
                        functions[count] = i->type.callable->function;
                        existing->overloads = functions;
                        existing->overload_count = count + 1;
                        existing->thisless &= function_is_thisless(i->type.callable->function);
                        prop_add_decl_spans(me, existing, i->declarations.data, (u32)vec_span_size(&i->declarations));
                    }
                    break;
                }
                Prop* prop = arena_new(&my arena, Prop);
                prop->name = name;
                prop->parent = i->origin_interface ? i->origin_interface : parent;
                prop->node = &i->type;
                if (i->has_write_type) prop->write_node = &i->write_type;
                prop->ctx = ctx;
                prop->ctx.method = is_method;
                prop->thisless = i->type.tag == TYPE_CALLABLE ?
                    function_is_thisless(i->type.callable->function) : type_node_is_thisless(&i->type);
                if (i->optional) prop->flags |= PROP_OPTIONAL;
                if (i->readonly) prop->flags |= PROP_READONLY;
                if (is_method) prop->flags |= PROP_METHOD;
                prop->name_span = i->key.span;
                prop->name_is_value = i->key.tag == INTERFACE_KEY_NAME && i->key.name_is_value;
                prop_add_decl_spans(me, prop, i->declarations.data, (u32)vec_span_size(&i->declarations));
                builder_push(builder, props, prop);
            }   break;

            case INTERFACE_KEY_INDEX: {
                // (tsc's getIndexInfosOfIndexSymbol: only a signature with exactly one typed parameter)
                if (!i->key.index.valid) break;
                // TODO: index signature value types are lowered eagerly, unlike property types
                IndexInfo info = {
                    .key = ty_from_node(me, &i->key.index.type, ctx),
                    .value = ty_from_node(me, &i->type, ctx),
                    .param_name = checker_atom(me, i->key.index.name.sv),
                    .readonly = i->readonly,
                    .decl_span = vec_span_size(&i->declarations) ? *vec_span_at(&i->declarations, 0) : (Span){0},
                };
                builder_push(builder, index, info);
            }   break;

            case INTERFACE_KEY_MAPPED: {
                LowerCtx mapped_ctx = ctx;
                mapped_ctx.generic = true;
                MappedInfo* mapped = arena_new(&my arena, MappedInfo);
                mapped->type_param = i->key.mapped.symbol_ref ?
                    ty_type_param(me, i->key.mapped.symbol_ref) :
                    ty_synthetic_type_param(me, i->key.mapped.type_name.sv);
                mapped->constraint = ty_from_node(me, &i->key.mapped.in_type, mapped_ctx);
                if (i->key.mapped.has_as_type) {
                    mapped->name_type = ty_from_node(me, &i->key.mapped.as_type, mapped_ctx);
                }
                mapped->template_node = &i->type;
                mapped->template_ctx = mapped_ctx;
                TypeNode const* in_type = &i->key.mapped.in_type;
                if (in_type->tag == TYPE_KEYOF) {
                    mapped->modifiers_type = ty_from_node(me, &in_type->keyof->interface, mapped_ctx);
                    mapped->keyof_constraint = true;
                } else if (mapped->constraint->kind == TY_TYPE_PARAM) {
                    // `[P in K]` with `K extends keyof T` (Pick) takes its modifiers from T too
                    Ty const* constraint = ty_param_constraint(me, mapped->constraint);
                    if (constraint && constraint->kind == TY_KEYOF) mapped->modifiers_type = constraint->keyof.operand;
                }
                mapped->readonly = i->key.mapped.readonly;
                mapped->optional = i->key.mapped.optional;
                builder->mapped = mapped;
            }   break;

            case INTERFACE_KEY_CALLABLE: {
                if (i->type.tag == TYPE_CALLABLE && builder->decl_group) {
                    set_decl_group(me, i->type.callable->function, builder->decl_group);
                }
                Signature* sig = i->type.tag == TYPE_CALLABLE ?
                    builder_add_callable(me, builder, i->type.callable->function, ctx) : nullptr;
                if (sig) {
                    sig->name_span = i->key.span;
                    if (vec_span_size(&i->declarations)) sig->decl_span = *vec_span_at(&i->declarations, 0);
                }
            }   break;
        }
    }
}

static void builder_add_interface(
    Checker* me, MembersBuilder* builder, Interface const* interface, LowerCtx ctx, Symbol const* parent
) {
    for_each (i, vec_interface_member, &interface->members) {
        builder_add_interface_member(me, builder, i, ctx, parent);
    }
}

// Every member of every part of an intersection (only the properties if `props_only`); a property several parts have
// is one of the intersection's own, with the intersected type (tsc's getPropertiesOfType on the base,
// createUnionOrIntersectionProperty)
static void builder_add_intersection_members(Checker* me, MembersBuilder* builder, Ty const* base, bool props_only) {
    for (u32 i = 0; i < base->composite.types.len; i++) {
        Ty const* part = base->composite.types.data[i];
        if (!(part->flags & TYF_OBJECT)) part = ty_apparent_type(me, part);
        if (!(part->flags & TYF_OBJECT)) continue;
        Members const* members = ty_members(me, part);
        for (u32 j = 0; j < members->prop_count; j++) {
            Prop* prop = members->props[j];
            if (builder_find_prop(builder, prop->name)) continue;
            u32 owners = 0;
            for (u32 k = 0; k < base->composite.types.len; k++) {
                Ty const* other = base->composite.types.data[k];
                owners += (other->flags & TYF_OBJECT) && ty_get_prop(me, other, prop->name);
            }
            if (owners > 1) {
                FoundProp found = ty_find_prop(me, base, prop->name);
                Prop* merged = arena_new(&my arena, Prop);
                *merged = (Prop){
                    .name = prop->name, .flags = found.flags, .parent = prop->parent, .target = prop,
                    .type = found.type, .type_state = LAZY_STATE_DONE,
                };
                prop = merged;
            }
            builder_push(builder, props, prop);
        }
        if (props_only) continue;
        for (u32 j = 0; j < members->call_count; j++) builder_push(builder, call, members->call[j]);
        for (u32 j = 0; j < members->construct_count; j++) builder_push(builder, construct, members->construct[j]);
        for (u32 j = 0; j < members->index_count; j++) {
            bool has_own = false;
            for (u32 k = 0; k < builder->index_count; k++) has_own |= builder->index[k].key == members->index[j].key;
            if (!has_own) builder_push(builder, index, members->index[j]);
        }
    }
}

static void builder_add_inherited(Checker* me, MembersBuilder* builder, Ty const* base) {
    // (A mixin's base can be a type parameter or `object`: what it has is its apparent type's)
    if (!(base->flags & (TYF_OBJECT | TYF_INTERSECTION))) base = ty_apparent_type(me, base);
    if (base->kind == TY_INTERSECTION) {
        builder_add_intersection_members(me, builder, base, false);
        return;
    }
    Members const* members = ty_members(me, base);
    for (u32 i = 0; i < members->prop_count; i++) {
        // A static `#x` stays the base's (tsc's isStaticPrivateIdentifierProperty in addInheritedMembers)
        if ((members->props[i]->flags & PROP_STATIC) && ty_private_name_class(me, members->props[i]->name)) continue;
        Prop* derived = builder_find_prop(builder, members->props[i]->name);
        if (!derived) {
            builder_push(builder, props, members->props[i]);
        } else if (derived->symbol && derived->symbol->decl_count && derived->symbol->decls[0].kind == SYMBOL_DECL_ASSIGNMENT &&
            !ty_is_constructor_declared(derived->symbol)
        ) {
            // A JavaScript `this.x = v` outside of the constructor doesn't override (tsc's addInheritedMembers)
            for (u32 j = 0; j < builder->props_count; j++) {
                if (builder->props[j] == derived) builder->props[j] = members->props[i];
            }
        }
    }
    // Signatures of the bases come after the type's own (they're overloads of each other). An index signature is
    // inherited unless the derived type declares its own
    for (u32 i = 0; i < members->call_count; i++) builder_push(builder, call, members->call[i]);
    for (u32 i = 0; i < members->construct_count; i++) builder_push(builder, construct, members->construct[i]);
    for (u32 i = 0; i < members->index_count; i++) {
        bool has_own = false;
        for (u32 j = 0; j < builder->index_count; j++) has_own |= builder->index[j].key == members->index[i].key;
        if (!has_own) builder_push(builder, index, members->index[i]);
    }
}

// Classes

// A base constructor signature a class that writes no type arguments gets (tsc's getInstantiatedConstructorsForTypeArguments
// and getDefaultConstructSignatures with none written): one without type parameters, or whose type parameters all have
// defaults, which the mapper (null if there are no type parameters) fills in. False if the signature doesn't qualify
static bool base_signature_without_type_args(Checker* me, Signature const* base_sig, TyMapper const** mapper) {
    signature_resolve(me, base_sig);
    TyList params = base_sig->type_params;
    *mapper = nullptr;
    if (!params.len) return true;
    Ty const** defaults = arena_new_n(&my arena, Ty const*, params.len);
    for (u32 j = 0; j < params.len; j++) {
        defaults[j] = ty_param_default(me, params.data[j]);
        if (!defaults[j]) return false;
    }
    *mapper = ty_mapper_new(me, params, (TyList){ .data = defaults, .len = params.len });
    return true;
}

// A construct signature of a base constructor that takes the written type arguments (tsc's
// getInstantiatedConstructorsForTypeArguments: as many as it has type parameters, or fewer if the rest have defaults):
// what it constructs with them. Null if it doesn't take them
static Ty const* base_signature_with_type_args(
    Checker* me, Signature const* base_sig, VecSpannedType const* written, LowerCtx ctx
) {
    signature_resolve(me, base_sig);
    TyList params = base_sig->type_params;
    u32 count = (u32)vec_spanned_type_size(written);
    if (count > params.len) return nullptr;
    Ty const** args = arena_new_n(&my arena, Ty const*, params.len ? params.len : 1);
    for (u32 j = 0; j < params.len; j++) {
        args[j] = j < count ? ty_from_node(me, &vec_spanned_type_at(written, j)->type, ctx) : ty_param_default(me, params.data[j]);
        if (!args[j]) return nullptr;
    }
    Ty const* ret = signature_return_type(me, base_sig);
    return params.len ? ty_instantiate(me, ret, ty_mapper_new(me, params, (TyList){ .data = args, .len = params.len })) : ret;
}

// An object type, `object`, a type parameter constrained to one or an intersection of them (tsc's isValidBaseType).
// Unlike tsc, not `any`: a class extending `any` has no base as far as class_base() goes
static bool is_valid_base_type(Checker* me, Ty const* ty, u32 depth) {
    if (depth > 16) return false;
    if (ty->flags & TYF_ANY) return false;
    if (ty->kind == TY_TYPE_PARAM) {
        Ty const* constraint = ty_param_constraint(me, ty);
        return constraint && constraint != ty && is_valid_base_type(me, constraint, depth + 1);
    }
    if (ty->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (!is_valid_base_type(me, ty->composite.types.data[i], depth + 1)) return false;
        }
        return true;
    }
    if (ty->flags & TYF_NON_PRIMITIVE) return true;
    if (!(ty->flags & TYF_OBJECT)) return false;
    return ty->kind != TY_ANONYMOUS || !(ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) || !ty_members(me, ty)->mapped;
}

// The class a class extends: its instance type (with the written type arguments) and its declaration. False if
// there's none. HACK: a class name (`extends Name`, `extends Name<Args>`, `extends A.B.Name`) is found syntactically;
// any other expression is the return type of its first construct signature that takes the written type arguments
// (tsc also checks that they all construct the same type, TS2510)
static bool class_base(Checker* me, ClassDecl const* class_decl, LowerCtx ctx, Ty const** instance, Symbol const** base_decl) {
    Expr const* extends = &class_decl->extends;
    VecSpannedType const* written_args = nullptr;
    if (extends->tag == EXPR_GENERIC_PARAMS) {
        written_args = &extends->generic_params->params;
        extends = &extends->generic_params->expr;
    }
    Symbol const* decl = nullptr;
    if (extends->tag == EXPR_IDENTIFIER) {
        decl = extends->identifier.target;
    } else if (extends->tag == EXPR_PROPERTY && extends->property->form == PROPERTY_EXPR_DOT) {
        // A class in a namespace
        Expr const* object = &extends->property->expr;
        Expr const* name = &extends->property->prop;
        Symbol const* namespace_decl = nullptr;
        if (object->tag == EXPR_IDENTIFIER) namespace_decl = object->identifier.target;
        namespace_decl = symbol_for_meaning(namespace_decl, SYMBOL_NAMESPACE);
        // (The name of a dot access is a literal)
        if (namespace_decl && !namespace_decl->is_placeholder && (name->tag == EXPR_IDENTIFIER || name->tag == EXPR_LITERAL)) {
            decl = symbol_get_export(namespace_decl, name->inner_span.sv);
        }
    }
    // The name was looked up as a value, the class is the same symbol
    decl = symbol_for_meaning(decl, SYMBOL_CLASS);
    if (!decl || decl->is_placeholder) {
        // Any other expression (a mixin call, a class expression, a variable of a constructor type): what its first
        // construct signature that takes the written type arguments returns. TODO: the static side, several signatures
        if (extends->tag == EXPR_IMPLICIT_ANY) return false;
        Ty const* constructor = ty_apparent_type(me, ty_expr_type(me, extends));
        if (!(constructor->flags & (TYF_OBJECT | TYF_INTERSECTION))) return false;
        Members const* members = ty_members(me, constructor);
        Ty const* constructed = nullptr;
        for (u32 i = 0; i < members->construct_count && !constructed; i++) {
            if (written_args) {
                constructed = base_signature_with_type_args(me, members->construct[i], written_args, ctx);
                continue;
            }
            TyMapper const* mapper;
            if (!base_signature_without_type_args(me, members->construct[i], &mapper)) continue;
            constructed = signature_return_type(me, members->construct[i]);
            if (mapper) constructed = ty_instantiate(me, constructed, mapper);
        }
        if (!constructed || !is_valid_base_type(me, constructed, 0)) return false;
        *instance = constructed;
        *base_decl = nullptr;
        return true;
    }
    Ty const* base = ty_declared_type_of(me, decl);
    if (base->kind != TY_DECLARED) return false;
    *base_decl = decl;
    TyList params = base->declared.type_params;
    if (!params.len) {
        *instance = base;
        return true;
    }
    Ty const* args[32];
    if (params.len > c_arraylen(args)) return false;
    u32 outer = base->declared.outer_count; // (A local base class: the same outer type parameters)
    for (u32 i = 0; i < params.len; i++) {
        if (i < outer) {
            args[i] = params.data[i];
        } else if (written_args && (isize_t)(i - outer) < vec_spanned_type_size(written_args)) {
            args[i] = ty_from_node(me, &vec_spanned_type_at(written_args, i - outer)->type, ctx);
        } else {
            Ty const* fallback = ty_param_default(me, params.data[i]);
            args[i] = fallback ? fallback : ty_intrinsic(me, TY_ANY);
        }
    }
    *instance = ty_reference(me, base, (TyList){ .data = args, .len = params.len });
    return true;
}

Ty const* ty_class_base_type_variable(Checker* me, Symbol const* class_symbol) {
    SymbolDecl const* node = symbol_find_decl(class_symbol, SYMBOL_DECL_CLASS);
    if (!node) return nullptr;
    ClassDecl const* class_decl = node->node;
    Expr const* extends = &class_decl->extends;
    if (extends->tag == EXPR_GENERIC_PARAMS) extends = &extends->generic_params->expr;
    if (extends->tag == EXPR_IMPLICIT_ANY) return nullptr;
    // (A class name isn't a type variable: no need to type the expression)
    if (extends->tag == EXPR_IDENTIFIER && symbol_for_meaning(extends->identifier.target, SYMBOL_CLASS)) return nullptr;
    SymbolLinks* links = ty_symbol_links(me, class_symbol);
    if (links->base_variable_state == LAZY_STATE_DONE) return links->base_variable;
    if (links->base_variable_state == LAZY_STATE_IN_FLIGHT) return nullptr; // The extends expression mentions the class
    links->base_variable_state = LAZY_STATE_IN_FLIGHT;
    Ty const* type = ty_expr_type(me, extends);
    Ty const* variable = nullptr;
    if ((type->flags & TYF_TYPE_PARAM) || type->kind == TY_INDEXED) {
        variable = type;
    } else if (type->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < type->composite.types.len && !variable; i++) {
            Ty const* part = type->composite.types.data[i];
            if ((part->flags & TYF_TYPE_PARAM) || part->kind == TY_INDEXED) variable = part;
        }
    }
    links = ty_symbol_links(me, class_symbol);
    links->base_variable = variable;
    links->base_variable_state = LAZY_STATE_DONE;
    return variable;
}

// Whether a class or interface declaration adds members to what it inherits: anything but statics (tsc's
// getMembersOfSymbol, where a constructor is a member too)
static bool declaration_has_members(SymbolDecl const* node) {
    if (node->kind == SYMBOL_DECL_INTERFACE) {
        InterfaceDecl const* interface_decl = node->node;
        return vec_interface_member_size(&interface_decl->type.interface.members) != 0;
    }
    if (node->kind != SYMBOL_DECL_CLASS) return false;
    for_each (i, vec_class_item, &((ClassDecl const*)node->node)->items) {
        if (i->tag == CLASS_ITEM_STATIC_BLOCK) continue;
        if (i->tag == CLASS_ITEM_INTERFACE_MEMBER ? !i->interface_member_static : !class_item_is_static(i)) return true;
    }
    return false;
}

static bool has_base_type(Checker* me, Ty const* source, Ty const* target, u32 depth);

static Ty const* non_augmenting_base_uncached(Checker* me, Ty const* ty) {
    Ty const* target = ty->kind == TY_REFERENCE ? ty->reference.target : ty;
    Symbol const* decl = target->declared.symbol;
    if (!decl || decl->members) return nullptr; // (JavaScript `this.x = v` members)
    // A class, or an interface that is a reference in tsc (generic, or mentioning `this`)
    bool is_class = target->declared.kind == TY_DECLARED_CLASS_INSTANCE;
    if (!is_class && !target->declared.type_params.len && !target->declared.this_param) return nullptr;
    LowerCtx ctx = { .generic = target->declared.type_params.len > 0 || target->declared.this_param };
    Ty const* base = nullptr;
    for (SymbolDecl const* node = decl->decls; node < decl->decls + decl->decl_count; node++) {
        if (declaration_has_members(node)) return nullptr;
        if (node->kind == SYMBOL_DECL_CLASS) {
            ClassDecl const* class_decl = node->node;
            Expr const* extends = &class_decl->extends;
            if (extends->tag == EXPR_IMPLICIT_ANY) continue;
            if (extends->tag == EXPR_GENERIC_PARAMS) extends = &extends->generic_params->expr;
            // (Only a name: the expression could mention the class itself)
            if (extends->tag != EXPR_IDENTIFIER && extends->tag != EXPR_PROPERTY) return nullptr;
            Ty const* instance = nullptr;
            Symbol const* base_decl = nullptr;
            if (!class_base(me, class_decl, ctx, &instance, &base_decl)) continue;
            if (base) return nullptr;
            base = instance;
        } else if (node->kind == SYMBOL_DECL_INTERFACE) {
            InterfaceDecl const* interface_decl = node->node;
            for_each (extends, vec_spanned_type, &interface_decl->extends) {
                Ty const* type = ty_from_node(me, &extends->type, ctx);
                if (!(type->flags & (TYF_OBJECT | TYF_INTERSECTION))) continue;
                if (base) return nullptr;
                base = type;
            }
        }
    }
    // (A circular base is none: tsc's resolveBaseTypesOfClass)
    if (!base || base == target || has_base_type(me, base, target, 0)) return nullptr;
    TyList params = target->declared.type_params;
    if (ty->kind == TY_REFERENCE && params.len) base = ty_instantiate(me, base, ty_mapper_new(me, params, ty_reference_args(me, ty)));
    if (target->declared.this_param) {
        Ty const* this_arg = ty->kind == TY_REFERENCE && ty->reference.this_arg ? ty->reference.this_arg : ty;
        base = ty_with_this_arg(me, base, this_arg);
    }
    return base;
}

Ty const* ty_non_augmenting_base(Checker* me, Ty const* ty) {
    TyObjectState* state;
    if (ty->kind == TY_DECLARED) {
        if (ty->declared.kind != TY_DECLARED_CLASS_INSTANCE && ty->declared.kind != TY_DECLARED_INTERFACE) return nullptr;
        if (ty->declared.merged_value) return nullptr;
        state = ty->declared.state;
    } else if (ty->kind == TY_REFERENCE) {
        Ty const* target = ty->reference.target;
        if (target->declared.kind != TY_DECLARED_CLASS_INSTANCE && target->declared.kind != TY_DECLARED_INTERFACE) return nullptr;
        if (ty->reference.deferred) return nullptr;
        state = ty->reference.state;
    } else {
        return nullptr;
    }
    if (!state) return nullptr;
    if (state->base_state == LAZY_STATE_DONE) return state->equivalent_base;
    if (state->base_state == LAZY_STATE_IN_FLIGHT) return nullptr;
    state->base_state = LAZY_STATE_IN_FLIGHT;
    Ty const* base = non_augmenting_base_uncached(me, ty);
    state->equivalent_base = base;
    state->base_state = LAZY_STATE_DONE;
    return base;
}

static u32 access_prop_flags(AccessModifier access) {
    return access == ACCESS_MODIFIER_PRIVATE ? PROP_PRIVATE : access == ACCESS_MODIFIER_PROTECTED ? PROP_PROTECTED : 0;
}

static Prop* builder_add_class_prop(
    Checker* me, MembersBuilder* builder, cstr const* name_string, Symbol const* parent, LowerCtx ctx, ClassItem const* item
) {
    Expr const* computed_key = item->tag == CLASS_ITEM_FIELD ? item->field.computed_key : item->method.computed_key;
    Atom const* name;
    if (computed_key) {
        // Only a literal or unique symbol key names a member (tsc's late bound names), anything else is left out
        bool is_number = false;
        name = ty_property_name_of_key(me, ty_expr_type(me, computed_key), &is_number);
        if (!name) return nullptr;
    } else if (span_valid(class_item_private_name(item)) && parent) {
        name = ty_private_name(me, parent, cstr_sv(name_string));
    } else {
        name = checker_atom(me, cstr_sv(name_string));
    }
    Prop* existing = builder_find_prop(builder, name);
    if (existing) return nullptr;
    Prop* prop = arena_new(&my arena, Prop);
    prop->name = name;
    prop->parent = parent;
    prop->ctx = ctx;
    prop->name_span = item->tag == CLASS_ITEM_FIELD ? item->field.name_span : item->method.name_span;
    prop->name_is_value = item->tag == CLASS_ITEM_FIELD ? item->field.name_is_value : item->method.name_is_value;
    prop->flags = access_prop_flags(item->access) | (class_item_is_static((ClassItem*)item) ? PROP_STATIC : 0);
    prop_add_decl_spans(me, prop, &item->span, 1);
    builder_push(builder, props, prop);
    return prop;
}

// The members JavaScript assignments declare (`this.x = v`, `F.prototype.x = v`: Symbol.members). One the class body
// declares keeps its declaration and type, the assignments are more declarations of it
static void builder_add_assignment_members(Checker* me, MembersBuilder* builder, Symbol const* symbol) {
    if (!symbol->members) return;
    u32 count;
    SymbolEntry* entries = symbol_table_collect(symbol->members, &count);
    for (u32 i = 0; i < count; i++) {
        Symbol const* member = entries[i].symbol;
        Atom const* name = checker_atom(me, entries[i].name);
        VecSpan spans = symbol_declaration_spans(member);
        Prop* existing = builder_find_prop(builder, name);
        // (One the class body declares has no assignment declarations: tsc's declareSymbol with isReplaceableByMethod)
        if (!existing) {
            Prop* prop = arena_new(&my arena, Prop);
            prop->name = name;
            prop->symbol = member;
            prop->parent = symbol;
            prop->ctx = (LowerCtx){ .generic = true };
            prop->name_span = member->name_span;
            prop_add_decl_spans(me, prop, spans.data, (u32)vec_span_size(&spans));
            // `/** @private */ this.x = v` (tsc's getDeclarationModifierFlagsFromSymbol: the value declaration's)
            uint8_t modifiers = member->decl_count ? member->decls[0].jsdoc_modifiers : 0;
            if (modifiers & JSDOC_MODIFIER_PRIVATE) prop->flags |= PROP_PRIVATE;
            else if (modifiers & JSDOC_MODIFIER_PROTECTED) prop->flags |= PROP_PROTECTED;
            if (modifiers & JSDOC_MODIFIER_READONLY) prop->flags |= PROP_READONLY;
            builder_push(builder, props, prop);
        }
        vec_span_drop(&spans);
    }
    free(entries);
    // `F.prototype = { ... }`: the object literal's members too (tsc's getAssignedClassSymbol, merged into the class by
    // getDeclaredTypeOfClassOrInterface)
    Symbol const* prototype = symbol->exports && !symbol_find_decl(symbol, SYMBOL_DECL_CLASS) ?
        symbol_table_get(symbol->exports, c_sv("prototype")) : nullptr;
    for (u32 i = 0; prototype && i < prototype->decl_count; i++) {
        Expr const* literal = prototype->decls[i].node;
        if (prototype->decls[i].kind != SYMBOL_DECL_ASSIGNMENT || literal->tag != EXPR_OBJECT) continue;
        Members const* members = ty_members(me, ty_widened_objects(me, ty_expr_type(me, literal)));
        for (u32 j = 0; j < members->prop_count; j++) {
            if (!builder_find_prop(builder, members->props[j]->name)) builder_push(builder, props, members->props[j]);
        }
        break;
    }
}

// A class member named by an expression of type string, number or symbol (not a literal or unique symbol, which name
// it) makes an index signature for that key of every member of the class (tsc's getIndexInfosOfIndexSymbol with
// hasLateBindableIndexSignature), unless the class declares one itself
static void builder_add_computed_index_infos(Checker* me, MembersBuilder* builder, ClassDecl const* class_decl, bool static_side) {
    static TyIntrinsic const keys[3] = { TY_STRING, TY_NUMBER, TY_ES_SYMBOL };
    bool computed[3] = {0}, readonly[3] = { true, true, true };
    for_each (i, vec_class_item, &class_decl->items) {
        bool field = i->tag == CLASS_ITEM_FIELD;
        if (!field && i->tag != CLASS_ITEM_METHOD) continue;
        if ((field ? i->field.is_static : i->method.is_static) != static_side) continue;
        Expr const* key = field ? i->field.computed_key : i->method.computed_key;
        if (!key || !expr_is_entity_name(key)) continue;
        Ty const* key_type = ty_regular(me, ty_expr_type(me, key));
        if (key_type->flags & (TYF_LITERAL | TYF_UNIQUE_ES_SYMBOL | TYF_ENUM | TYF_ANY | TYF_NEVER)) continue;
        u32 k = ty_is_related(me, key_type, ty_intrinsic(me, TY_NUMBER), TY_RELATION_ASSIGNABLE) ? 1 :
            ty_is_related(me, key_type, ty_intrinsic(me, TY_ES_SYMBOL), TY_RELATION_ASSIGNABLE) ? 2 :
            ty_is_related(me, key_type, ty_intrinsic(me, TY_STRING), TY_RELATION_ASSIGNABLE) ? 0 : 3;
        if (k == 3) continue;
        computed[k] = true;
        readonly[k] &= field && i->field.is_readonly;
    }
    for (u32 k = 0; k < 3; k++) {
        if (!computed[k]) continue;
        Ty const* key = ty_intrinsic(me, keys[k]);
        bool declared = false;
        for (u32 j = 0; j < builder->index_count; j++) declared |= builder->index[j].key == key;
        if (declared) continue;
        // The value is what every member of that kind of name has: the computed ones and the others (getObjectLiteralIndexInfo)
        TyBuf types = {0};
        for (u32 j = 0; j < builder->props_count; j++) {
            Atom const* name = builder->props[j]->name;
            bool symbol_name = ty_unique_symbol_of_name(me, name) != nullptr;
            bool included = k == 0 ? !symbol_name : k == 1 ? ty_is_numeric_name(name) : symbol_name;
            // (A class's `prototype` isn't one of its members in tsc, it's made with the static side's type)
            if (static_side && csview_equals(atom_sv(name), "prototype")) continue;
            if (included) ty_buf_push(&types, prop_type(me, builder->props[j]));
        }
        for_each (i, vec_class_item, &class_decl->items) {
            bool field = i->tag == CLASS_ITEM_FIELD;
            if (!field && i->tag != CLASS_ITEM_METHOD) continue;
            if ((field ? i->field.is_static : i->method.is_static) != static_side) continue;
            Expr const* name_expr = field ? i->field.computed_key : i->method.computed_key;
            if (!name_expr || !expr_is_entity_name(name_expr)) continue;
            Ty const* key_type = ty_regular(me, ty_expr_type(me, name_expr));
            if (key_type->flags & (TYF_LITERAL | TYF_UNIQUE_ES_SYMBOL | TYF_ENUM | TYF_ANY | TYF_NEVER)) continue;
            if (!ty_is_related(me, key_type, key, TY_RELATION_ASSIGNABLE) && k != 0) continue;
            if (!field && i->method.accessor && i->method.accessor_first != i) continue; // One type per accessor pair
            ty_buf_push(&types, field || i->method.accessor ? class_item_get_type((ClassItem*)i) : ty_function_type(me, &i->method.func));
        }
        IndexInfo info = {
            .key = key,
            .value = types.len ? ty_union_subtype_reduced(me, types.data, types.len) : ty_intrinsic(me, TY_UNDEFINED),
            .param_name = checker_atom(me, c_sv("x")),
            .readonly = readonly[k],
        };
        ty_buf_drop(&types);
        builder_push(builder, index, info);
    }
}

// The members a class declares itself, on the instance or the static side
static void builder_add_class_items(
    Checker* me, MembersBuilder* builder, ClassDecl const* class_decl, bool static_side, LowerCtx ctx, Symbol const* parent
) {
    for_each (i, vec_class_item, &class_decl->items) {
        switch (i->tag) {
            case CLASS_ITEM_FIELD: {
                if (i->field.is_static != static_side) break;
                Prop* prop = builder_add_class_prop(me, builder, &i->field.name_string, parent, ctx, i);
                if (!prop) break;
                if (i->field.has_explicit_type) {
                    prop->node = &i->field.explicit_type;
                } else {
                    prop->lazy = &i->field.lazy_type;
                }
                prop->thisless = binding_is_thisless(i->field.has_explicit_type, &i->field.explicit_type,
                    i->field.value.tag != EXPR_IMPLICIT_ANY);
                if (!i->field.is_auto_accessor) prop->flags |= PROP_FIELD;
                if (i->field.is_optional) prop->flags |= PROP_OPTIONAL;
                if (i->field.is_readonly) prop->flags |= PROP_READONLY;
            }   break;

            case CLASS_ITEM_METHOD: {
                if (i->method.is_static != static_side) break;
                if (i->method.accessor) {
                    if (i->method.accessor_first != i) break; // The second half of a get/set pair
                    Prop* prop = builder_add_class_prop(me, builder, &i->method.name_string, parent, ctx, i);
                    if (!prop) break;
                    prop->lazy = &i->method.accessor_type;
                    prop->flags |= PROP_ACCESSOR;
                    // Reads go by the getter's accessibility, writes by the setter's
                    bool has_setter = false, has_getter = false;
                    TypeNode const* setter_type = nullptr;
                    u32 get_access = access_prop_flags(i->access), set_access = get_access;
                    for_each (other, vec_class_item, &class_decl->items) {
                        if (other->tag != CLASS_ITEM_METHOD || other->method.accessor_first != i) continue;
                        has_setter |= other->method.accessor == CLASS_ACCESSOR_SET;
                        has_getter |= other->method.accessor == CLASS_ACCESSOR_GET;
                        FunctionImpl const* func = &other->method.func;
                        if (other->method.accessor == CLASS_ACCESSOR_SET && vec_function_arg_size(&func->args) &&
                            func->args.data[0].initializer.binding.has_explicit_type
                        ) {
                            setter_type = &func->args.data[0].initializer.binding.explicit_type;
                        }
                        if (other != i) prop_add_decl_spans(me, prop, &other->span, 1);
                        if (other->method.accessor == CLASS_ACCESSOR_GET) get_access = access_prop_flags(other->access);
                        if (other->method.accessor == CLASS_ACCESSOR_SET) set_access = access_prop_flags(other->access);
                    }
                    if (!has_setter) set_access = get_access;
                    prop->flags = (prop->flags & ~(u32)(PROP_PRIVATE | PROP_PROTECTED)) | get_access |
                        (set_access & PROP_PRIVATE ? PROP_SET_PRIVATE : 0) | (set_access & PROP_PROTECTED ? PROP_SET_PROTECTED : 0);
                    if (!has_setter) prop->flags |= PROP_READONLY;
                    if (has_getter) prop->write_node = setter_type; // Writes go by the setter's parameter type
                    break;
                }
                // With overload signatures around, the implementation's own signature isn't visible
                bool has_overloads = false;
                u32 visible = 0;
                for_each (other, vec_class_item, &class_decl->items) {
                    if (other->tag != CLASS_ITEM_METHOD || other->method.accessor || other->method.is_static != static_side ||
                        !cstr_eq(&other->method.name_string, &i->method.name_string)
                    ) {
                        continue;
                    }
                    if (!visible && other != i) goto next_item; // Not the first declaration of this method
                    visible++;
                    has_overloads |= !other->method.func.has_body;
                }
                {
                    bool abstract_only = true; // Abstract methods have no body either, and aren't overloads of anything
                    for_each (other, vec_class_item, &class_decl->items) {
                        if (other->tag == CLASS_ITEM_METHOD && !other->method.accessor && other->method.is_static == static_side &&
                            cstr_eq(&other->method.name_string, &i->method.name_string) && other->method.func.has_body
                        ) {
                            abstract_only = false;
                        }
                    }
                    if (abstract_only) has_overloads = false;
                    FunctionImpl const** functions = arena_new_n(&my arena, FunctionImpl const*, visible);
                    u32 count = 0;
                    ClassItem const* previous = nullptr; // The class item before `other`
                    for_each (other, vec_class_item, &class_decl->items) {
                        ClassItem const* before = previous;
                        previous = other;
                        if (other->tag != CLASS_ITEM_METHOD || other->method.accessor || other->method.is_static != static_side ||
                            !cstr_eq(&other->method.name_string, &i->method.name_string)
                        ) {
                            continue;
                        }
                        if (has_overloads && other->method.func.has_body) continue;
                        // A body right after a declaration of the same method is its implementation, even after another
                        // body (tsc's getSignaturesOfSymbol: `m() {} m(x) {}` has one signature)
                        if (count && other->method.func.has_body && before && before->tag == CLASS_ITEM_METHOD &&
                            !before->method.accessor && before->method.is_static == static_side &&
                            cstr_eq(&before->method.name_string, &i->method.name_string)
                        ) {
                            continue;
                        }
                        functions[count++] = &other->method.func;
                    }
                    // JSDoc `@overload`s instead of the method's own signature
                    if (count == 1 && functions[0]->jsdoc_overload_count) {
                        FunctionImpl const* method = functions[0];
                        count = method->jsdoc_overload_count;
                        functions = arena_new_n(&my arena, FunctionImpl const*, count);
                        for (u32 f = 0; f < count; f++) functions[f] = method->jsdoc_overloads[f].callable->function;
                    }
                    Span* spans = arena_new_n(&my arena, Span, visible);
                    u32 span_count = 0;
                    for_each (other, vec_class_item, &class_decl->items) {
                        if (other != i && other->tag == CLASS_ITEM_METHOD && !other->method.accessor &&
                            other->method.is_static == static_side && cstr_eq(&other->method.name_string, &i->method.name_string)
                        ) {
                            spans[span_count++] = other->span;
                        }
                    }
                    if (!count) break;
                    LowerCtx method_ctx = ctx;
                    method_ctx.method = true;
                    Prop* prop = builder_add_class_prop(me, builder, &i->method.name_string, parent, method_ctx, i);
                    if (!prop) break;
                    prop->function = functions[0];
                    prop->flags |= PROP_METHOD;
                    prop->thisless = true;
                    for (u32 f = 0; f < count; f++) prop->thisless &= function_is_thisless(functions[f]);
                    prop_add_decl_spans(me, prop, spans, span_count);
                    if (i->method.is_optional) prop->flags |= PROP_OPTIONAL;
                    if (count > 1) {
                        prop->overloads = functions;
                        prop->overload_count = count;
                    }
                }
            }   break;

            case CLASS_ITEM_INTERFACE_MEMBER:
                if (static_side == i->interface_member_static) {
                    builder_add_interface_member(me, builder, &i->interface_memeber, ctx, parent);
                }
                break;

            case CLASS_ITEM_CONSTRUCTOR:
                // Parameter properties: `constructor(public x: T)`
                if (static_side) break;
                for_each (arg, vec_function_arg, &i->constructor.func.args) {
                    Binding const* binding = &arg->initializer.binding;
                    if (!arg->is_parameter_property || binding->tag != BINDING_IDENTIFIER) continue;
                    Atom const* name = checker_atom(me, binding->content_span.sv);
                    if (builder_find_prop(builder, name)) continue;
                    Prop* prop = arena_new(&my arena, Prop);
                    prop->name = name;
                    prop->parent = parent;
                    prop->ctx = ctx;
                    prop->flags = access_prop_flags(arg->access);
                    prop->name_span = binding->content_span;
                    prop_add_decl_spans(me, prop, &arg->span, 1);
                    if (binding->has_explicit_type) {
                        prop->node = &binding->explicit_type;
                    } else {
                        prop->lazy = &arg->initializer.lazy_type;
                    }
                    prop->thisless = binding_is_thisless(binding->has_explicit_type, &binding->explicit_type,
                        arg->initializer.value.tag != EXPR_IMPLICIT_ANY);
                    if (arg->explicit_optional) prop->flags |= PROP_OPTIONAL;
                    if (arg->is_readonly_property) prop->flags |= PROP_READONLY;
                    builder_push(builder, props, prop);
                }
                break;

            case CLASS_ITEM_STATIC_BLOCK:
                break;
        }
        next_item:;
    }
    builder_add_computed_index_infos(me, builder, class_decl, static_side);
}

static Signature* class_construct_signature(Checker* me, ClassDecl const* class_decl, Ty const* instance_declared) {
    Signature* sig = arena_new(&my arena, Signature);
    sig->flags = SIGNATURE_CONSTRUCT | (class_decl->is_abstract ? SIGNATURE_ABSTRACT : 0);
    // (The outer type parameters of a local class aren't the signature's: the static side instantiates them)
    sig->class_type_params = ty_local_type_params(instance_declared);
    sig->class_instance = instance_declared->declared.type_params.len ?
        ty_reference(me, instance_declared, instance_declared->declared.type_params) :
        instance_declared;
    sig->ctx = (LowerCtx){ .generic = instance_declared->declared.type_params.len > 0 };
    return sig;
}

static void builder_add_class_static_side(Checker* me, MembersBuilder* builder, Ty const* ty, ClassDecl const* class_decl) {
    Symbol const* instance_decl = class_decl->symbol;
    Ty const* instance = instance_decl ? ty_declared_type_of(me, instance_decl) : nullptr;
    if (!instance || instance->kind != TY_DECLARED) return;
    LowerCtx ctx = { .generic = instance->declared.type_params.len > 0 };

    // `prototype` is the instance type with `any` for every type parameter
    Prop* prototype = arena_new(&my arena, Prop);
    prototype->name = checker_atom(me, c_sv("prototype"));
    prototype->parent = instance_decl;
    prototype->flags = PROP_STATIC; // Not readonly: tsc's prototype symbol is a plain property
    if (instance->declared.type_params.len) {
        u32 len = instance->declared.type_params.len;
        Ty const** args = arena_new_n(&my arena, Ty const*, len);
        for (u32 i = 0; i < len; i++) args[i] = ty_intrinsic(me, TY_ANY);
        prototype->type = ty_reference(me, instance, (TyList){ .data = args, .len = len });
    } else {
        prototype->type = instance;
    }
    prototype->type_state = LAZY_STATE_DONE;
    builder_push(builder, props, prototype);

    builder_add_class_items(me, builder, class_decl, true, ctx, instance_decl);

    // Construct signatures: the constructor's overload signatures, else its implementation, else the base class's
    bool has_constructor = false, has_overloads = false;
    for_each (i, vec_class_item, &class_decl->items) {
        if (i->tag != CLASS_ITEM_CONSTRUCTOR) continue;
        has_constructor = true;
        has_overloads |= !i->constructor.func.has_body;
    }
    for_each (i, vec_class_item, &class_decl->items) {
        if (i->tag != CLASS_ITEM_CONSTRUCTOR || (has_overloads && i->constructor.func.has_body)) continue;
        Signature* sig = class_construct_signature(me, class_decl, instance);
        sig->function = &i->constructor.func;
        sig->name_span = i->constructor.name_span;
        sig->decl_span = i->constructor.func.full_span;
        builder_push(builder, construct, sig);
    }

    Ty const* base_instance = nullptr;
    Symbol const* base_decl = nullptr;
    bool has_base = class_base(me, class_decl, ctx, &base_instance, &base_decl);
    Ty const* base_static = nullptr;
    if (has_base && base_decl) base_static = ty_static_type_of(me, base_decl);
    if (base_static == ty || (base_static && base_static->kind != TY_DECLARED)) base_static = nullptr;

    // The type of the extends expression, when the base isn't a class declaration (a variable of a constructor type, a
    // mixin call...)
    Ty const* base_expr_constructor = nullptr;
    // (Also when what it constructs isn't a valid base, `any`: the constructor still is one)
    if (!base_static && class_decl->extends.tag != EXPR_IMPLICIT_ANY) {
        Expr const* extends = &class_decl->extends;
        if (extends->tag == EXPR_GENERIC_PARAMS) extends = &extends->generic_params->expr;
        Ty const* type = ty_apparent_type(me, ty_expr_type(me, extends));
        if (type->flags & (TYF_OBJECT | TYF_INTERSECTION) && type != ty) base_expr_constructor = type;
    }

    if (!has_constructor) {
        Members const* base_members = base_static ? ty_members(me, base_static) : nullptr;
        Members const* expr_members = base_expr_constructor ? ty_members(me, base_expr_constructor) : nullptr;
        if (!base_members && expr_members && expr_members->construct_count) {
            // tsc's getDefaultConstructSignatures: the base constructor's signatures that take no type arguments (none
            // are written: class_base() gives up on those), returning this class
            for (u32 i = 0; i < expr_members->construct_count; i++) {
                Signature const* base_sig = expr_members->construct[i];
                TyMapper const* mapper;
                if (!base_signature_without_type_args(me, base_sig, &mapper)) continue;
                Signature* sig = class_construct_signature(me, class_decl, instance);
                sig->target = base_sig;
                sig->mapper = mapper;
                sig->drops_mapped_type_params = mapper != nullptr;
                builder_push(builder, construct, sig);
            }
        } else if (base_members && base_members->construct_count) {
            TyMapper const* mapper = base_instance->kind == TY_REFERENCE ?
                ty_mapper_new(me, base_instance->reference.target->declared.type_params, ty_reference_args(me, base_instance)) :
                nullptr;
            for (u32 i = 0; i < base_members->construct_count; i++) {
                Signature* sig = class_construct_signature(me, class_decl, instance);
                sig->target = base_members->construct[i];
                sig->mapper = mapper;
                builder_push(builder, construct, sig);
            }
        } else {
            builder_push(builder, construct, class_construct_signature(me, class_decl, instance));
        }
    }

    // Static members are inherited too: every property of the base constructor's type (tsc's
    // getBaseConstructorTypeOfClass), which also has what merges into a base class and what the type of a constructor
    // that isn't a class has
    Ty const* base_constructor = base_static;
    if (has_base) {
        Expr const* extends = &class_decl->extends;
        if (extends->tag == EXPR_GENERIC_PARAMS) extends = &extends->generic_params->expr;
        Ty const* type = ty_apparent_type(me, ty_expr_type(me, extends));
        if (type->flags & (TYF_OBJECT | TYF_INTERSECTION) && type != ty) base_constructor = type;
    }
    if (base_constructor && base_constructor->kind == TY_INTERSECTION) {
        builder_add_intersection_members(me, builder, base_constructor, true);
    } else if (base_constructor) {
        Members const* base_members = ty_members(me, base_constructor);
        for (u32 i = 0; i < base_members->prop_count; i++) {
            // A static `#x` isn't inherited (tsc's addInheritedMembers: isStaticPrivateIdentifierProperty)
            if (ty_private_name_class(me, base_members->props[i]->name)) continue;
            if (!builder_find_prop(builder, base_members->props[i]->name)) {
                builder_push(builder, props, base_members->props[i]);
            }
        }
    }
}

static Prop* namespace_export_prop(Checker* me, SymbolEntry const* entry);

// A type-only export of a namespace or module (`export type { A }`, or reached through `export type *`)
static bool namespace_export_is_type_only(Symbol const* namespace, SymbolEntry const* entry) {
    return symbol_type_only_declaration(entry->symbol) ||
        module_type_only_star((Symbol*)symbol_members(namespace), entry->name);
}

Prop const* ty_namespace_type_only_prop(Checker* me, Ty const* object, Atom const* name) {
    if (object->kind != TY_DECLARED || object->declared.kind != TY_DECLARED_NAMESPACE) return nullptr;
    Symbol const* namespace = object->declared.namespace_symbol;
    csview sv = atom_sv(name);
    Symbol* symbol = symbol_get_export(namespace, sv);
    if (!symbol) return nullptr;
    SymbolEntry entry = { .hash = symbol_name_hash(sv), .name = sv, .symbol = symbol };
    if (!namespace_export_is_type_only(namespace, &entry)) return nullptr;
    u64 key[3] = { KEY_TYPE_ONLY_EXPORT, (u64)(uintptr_t)namespace, (u64)(uintptr_t)symbol };
    Prop const* existing = intern_get(&my links, key, 3);
    if (existing) return existing;
    Prop* made = namespace_export_prop(me, &entry);
    *intern_slot(me, &my links, key, 3) = made;
    return made;
}

// The property a namespace's export makes of its value, null if it has no value
static Prop* namespace_export_prop(Checker* me, SymbolEntry const* entry) {
    Symbol const* symbol = entry->symbol;
    Symbol const* member = symbol_for_meaning(symbol, SYMBOL_VALUE);
    if (!member || member->is_placeholder) return nullptr;
    Prop* prop = arena_new(&my arena, Prop);
    prop->name = checker_atom(me, entry->name);
    prop->symbol = member;
    if (member->is_const) prop->flags |= PROP_READONLY; // tsc's isReadonlySymbol: a `const` variable
    // An expando assigned a function is a method (bindPotentiallyNewExpandoMemberToNamespace)
    if (member->decl_count && member->decls[0].kind == SYMBOL_DECL_ASSIGNMENT && member->parent &&
        !member->parent->is_module && ((Expr const*)member->decls[0].node)->tag == EXPR_FUNCTION
    ) {
        prop->flags |= PROP_METHOD;
    }
    prop->ctx = (LowerCtx){ .generic = true };
    prop->name_span = symbol->name_span;
    // `N.p` in the symbols baseline
    if (symbol->parent && !symbol->parent->is_module) prop->parent = symbol->parent;
    VecSpan spans = symbol_declaration_spans(symbol);
    prop_add_decl_spans(me, prop, spans.data, (u32)vec_span_size(&spans));
    vec_span_drop(&spans);
    return prop;
}

// The members of every value declaration of a merged declaration, see ty_merged_value_type()
static void builder_add_merged_value(Checker* me, MembersBuilder* builder, Symbol const* decl) {
    Ty const** entries;
    u32 entry_count = ty_merged_value_entries(me, decl, &entries);
    for (u32 entry_index = 0; entry_index < entry_count; entry_index++) {
        Ty const* entry = entries[entry_index];
        if (!(entry->flags & TYF_OBJECT)) continue;
        Members const* members = ty_members(me, entry);
        for (u32 j = 0; j < members->prop_count; j++) {
            Prop* existing = builder_find_prop(builder, members->props[j]->name);
            if (existing && existing->parent && existing->parent != decl && members->props[j]->parent == decl) {
                // The symbol's own export (a JavaScript class's expando `C.x = v`) hides a static the class inherits
                // (tsc: the class's exports come before the base constructor's members). The props here are copies
                *existing = *members->props[j];
                if (existing->type_state != LAZY_STATE_DONE) existing->type_state = LAZY_STATE_NOT_STARTED;
            } else if (existing) {
                prop_add_decl_spans(me, existing, members->props[j]->decl_spans, members->props[j]->decl_span_count);
            } else {
                // A copy: decl spans are added to it
                Prop* prop = arena_new(&my arena, Prop);
                *prop = *members->props[j];
                if (prop->type_state != LAZY_STATE_DONE) prop->type_state = LAZY_STATE_NOT_STARTED;
                builder_push(builder, props, prop);
            }
        }
        for (u32 j = 0; j < members->call_count; j++) builder_push(builder, call, members->call[j]);
        for (u32 j = 0; j < members->construct_count; j++) builder_push(builder, construct, members->construct[j]);
        for (u32 j = 0; j < members->index_count; j++) builder_push(builder, index, members->index[j]);
    }
    free(entries);
    // What a module's `export =` assigns, with the module's other exports (module_export_equals())
    if (decl->merged_export_equals_of) {
        for (u32 i = 0; i < decl->exports->count; i++) {
            SymbolEntry const* entry = &decl->exports->entries[i];
            if (builder_find_prop(builder, checker_atom(me, entry->name))) continue;
            Prop* prop = namespace_export_prop(me, entry);
            if (!prop) continue;
            // (`module.exports.f = f` of a `const f` is an alias, which isn't readonly: tsc's isReadonlySymbol)
            if (entry->symbol->flags & SYMBOL_ALIAS) prop->flags &= ~PROP_READONLY;
            builder_push(builder, props, prop);
        }
    }
}

static void builder_add_enum_object(Checker* me, MembersBuilder* builder, EnumDecl const* enum_decl) {
    bool any_numeric = false;
    for_each (i, vec_enum_case, &enum_decl->cases) {
        if (!i->symbol) continue;
        Atom const* name = checker_atom(me, i->name.sv);
        if (builder_find_prop(builder, name)) continue;
        Prop* prop = arena_new(&my arena, Prop);
        prop->name = name;
        prop->parent = enum_decl->symbol;
        prop->flags = PROP_READONLY;
        prop->type = ty_declared_type_of(me, i->symbol);
        prop->type_state = LAZY_STATE_DONE;
        prop->name_span = i->full_name;
        prop->name_is_value = i->name_is_value;
        prop_add_decl_spans(me, prop, &i->full_name, 1);
        any_numeric |= !(prop->type->flags & TYF_STRING_LITERAL);
        builder_push(builder, props, prop);
    }
    // The reverse mapping of numeric members
    if (any_numeric && !enum_decl->is_const && !builder->index_count) {
        IndexInfo info = {
            .key = ty_intrinsic(me, TY_NUMBER),
            .value = ty_intrinsic(me, TY_STRING),
            .param_name = checker_atom(me, c_sv("x")),
            .readonly = true,
        };
        builder_push(builder, index, info);
    }
}

// tsc's hasBaseType(): whether the class or interface `source` is, extends or (transitively) inherits from `target`.
// Type arguments don't matter
static bool has_base_type(Checker* me, Ty const* source, Ty const* target, u32 depth) {
    if (source->kind == TY_REFERENCE) source = source->reference.target;
    if (source == target) return true;
    if (depth > 32) return false;
    if (source->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < source->composite.types.len; i++) {
            if (has_base_type(me, source->composite.types.data[i], target, depth + 1)) return true;
        }
        return false;
    }
    if (source->kind != TY_DECLARED) return false;
    if (source->declared.kind != TY_DECLARED_INTERFACE && source->declared.kind != TY_DECLARED_CLASS_INSTANCE) return false;
    Symbol const* decl = source->declared.symbol;
    if (!decl) return false;
    LowerCtx ctx = { .generic = source->declared.type_params.len > 0 };
    for (SymbolDecl const* node = decl->decls; node < decl->decls + decl->decl_count; node++) {
        if (node->kind == SYMBOL_DECL_CLASS) {
            Ty const* base = nullptr;
            Symbol const* base_decl = nullptr;
            if (class_base(me, node->node, ctx, &base, &base_decl) && base != source && has_base_type(me, base, target, depth + 1)) {
                return true;
            }
        } else if (node->kind == SYMBOL_DECL_INTERFACE) {
            InterfaceDecl const* interface_decl = node->node;
            for_each (extends, vec_spanned_type, &interface_decl->extends) {
                Ty const* base = ty_from_node(me, &extends->type, ctx);
                if (base != source && has_base_type(me, base, target, depth + 1)) return true;
            }
        }
    }
    return false;
}

bool ty_has_base_type(Checker* me, Ty const* source, Ty const* target) {
    if (target->kind == TY_REFERENCE) target = target->reference.target;
    return has_base_type(me, source, target, 0);
}

enum { BASE_CYCLE_KNOWN = 1, BASE_CYCLE_IN_CYCLE = 2 };

typedef struct BaseWalk {
    Symbol const* target;
    Symbol const* visited[256];
    u32 visited_count;
} BaseWalk;

// The interface an `extends` of an interface names, null if it isn't one
static Symbol const* interface_base_symbol(Checker* me, TypeNode const* node, LowerCtx ctx) {
    Ty const* base = ty_from_node(me, node, ctx);
    if (base->kind == TY_REFERENCE) base = base->reference.target;
    if (base->kind != TY_DECLARED || base->declared.kind != TY_DECLARED_INTERFACE) return nullptr;
    return base->declared.symbol;
}

// Whether the interface `symbol` is or (transitively) extends walk->target, each interface visited once
static bool interface_reaches(Checker* me, Symbol const* symbol, BaseWalk* walk) {
    if (symbol == walk->target) return true;
    for (u32 i = 0; i < walk->visited_count; i++) {
        if (walk->visited[i] == symbol) return false;
    }
    if (walk->visited_count == c_arraylen(walk->visited)) return false; // (Bigger than anything real)
    walk->visited[walk->visited_count++] = symbol;
    Ty const* declared = ty_declared_type_of(me, symbol);
    LowerCtx ctx = { .generic = declared->kind == TY_DECLARED && declared->declared.type_params.len > 0 };
    for (SymbolDecl const* node = symbol->decls; node < symbol->decls + symbol->decl_count; node++) {
        if (node->kind != SYMBOL_DECL_INTERFACE) continue;
        InterfaceDecl const* interface_decl = node->node;
        for_each (extends, vec_spanned_type, &interface_decl->extends) {
            Symbol const* base = interface_base_symbol(me, &extends->type, ctx);
            if (base && interface_reaches(me, base, walk)) return true;
        }
    }
    return false;
}

bool ty_interface_in_base_cycle(Checker* me, Symbol const* symbol) {
    SymbolLinks* links = ty_symbol_links(me, symbol);
    if (!(links->base_cycle & BASE_CYCLE_KNOWN)) {
        links->base_cycle |= BASE_CYCLE_KNOWN;
        BaseWalk* walk = malloc(sizeof(BaseWalk));
        panic_if(!walk, "out of memory");
        walk->target = symbol;
        walk->visited_count = 1;
        walk->visited[0] = symbol; // (The walk starts at its bases)
        Ty const* declared = ty_declared_type_of(me, symbol);
        LowerCtx ctx = { .generic = declared->kind == TY_DECLARED && declared->declared.type_params.len > 0 };
        bool found = false;
        for (SymbolDecl const* node = symbol->decls; node < symbol->decls + symbol->decl_count && !found; node++) {
            if (node->kind != SYMBOL_DECL_INTERFACE) continue;
            InterfaceDecl const* interface_decl = node->node;
            for_each (extends, vec_spanned_type, &interface_decl->extends) {
                Symbol const* base = interface_base_symbol(me, &extends->type, ctx);
                if (base && interface_reaches(me, base, walk)) {
                    found = true;
                    break;
                }
            }
        }
        free(walk);
        if (found) links->base_cycle |= BASE_CYCLE_IN_CYCLE;
    }
    return links->base_cycle & BASE_CYCLE_IN_CYCLE;
}

// Whether a class (instance or static side) has a base that class_base() can't see: an `extends` expression it can't
// resolve or that constructs `any` (tsc: the class gets anyBaseTypeIndexInfo, `[x: string]: any`). Members can be
// missing from such a class
static bool has_unresolved_base(Checker* me, Ty const* ty, u32 depth) {
    if (ty->kind == TY_REFERENCE) ty = ty->reference.target;
    if (depth > 32 || ty->kind != TY_DECLARED) return false;
    if (ty->declared.kind != TY_DECLARED_CLASS_INSTANCE && ty->declared.kind != TY_DECLARED_CLASS_STATIC) return false;
    Symbol const* decl = ty->declared.symbol;
    if (!decl) return false;
    LowerCtx ctx = { .generic = ty->declared.type_params.len > 0 };
    for (SymbolDecl const* node = decl->decls; node < decl->decls + decl->decl_count; node++) {
        if (node->kind != SYMBOL_DECL_CLASS) continue;
        ClassDecl const* class_decl = node->node;
        if (class_decl->extends.tag == EXPR_IMPLICIT_ANY) continue;
        Ty const* base = nullptr;
        Symbol const* base_decl = nullptr;
        if (!class_base(me, class_decl, ctx, &base, &base_decl)) return true;
        if (base != ty && has_unresolved_base(me, base, depth + 1)) return true;
    }
    return false;
}

bool ty_has_unresolved_base(Checker* me, Ty const* ty) {
    return has_unresolved_base(me, ty, 0);
}

Ty const* ty_class_base_instance(Checker* me, ClassDecl const* class_decl) {
    Ty const* base = nullptr;
    Symbol const* base_decl = nullptr;
    Ty const* declared = class_decl->symbol ? ty_declared_type_of(me, class_decl->symbol) : nullptr;
    LowerCtx ctx = { .generic = declared && declared->kind == TY_DECLARED &&
        (declared->declared.type_params.len || declared->declared.this_param) };
    return class_base(me, class_decl, ctx, &base, &base_decl) ? base : nullptr;
}

Atom const* ty_member_name(Checker* me, cstr const* name_string, Expr const* computed_key) {
    if (!computed_key) return checker_atom(me, cstr_sv(name_string));
    bool is_number = false;
    return ty_property_name_of_key(me, ty_expr_type(me, computed_key), &is_number);
}

static Members const* resolve_tuple_members(Checker* me, Ty const* tuple);

// The members as declared: own members, then what the bases add. For a type with a `this` type these mention it, and
// the bases are taken with the this-argument being it (tsc's resolveDeclaredMembers + the base part of
// resolveObjectTypeMembers)
static Members const* resolve_declared_members(Checker* me, Ty const* ty) {
    Symbol const* decl = ty->declared.symbol;
    Ty const* this_param = ty->declared.this_param;
    LowerCtx ctx = { .generic = ty->declared.type_params.len > 0 || this_param };
    MembersBuilder builder = {0};
    if (ty->declared.merged_value) {
        builder_add_merged_value(me, &builder, ty->declared.merged_value);
        return builder_finish(me, &builder);
    }
    switch (ty->declared.kind) {
        case TY_DECLARED_INTERFACE:
        case TY_DECLARED_CLASS_INSTANCE: {
            // Own members of every merged declaration first, then what their bases add
            for (SymbolDecl const* node = decl->decls; node < decl->decls + decl->decl_count; node++) {
                if (node->kind == SYMBOL_DECL_INTERFACE) {
                    InterfaceDecl const* interface_decl = node->node;
                    check(interface_decl->type.tag == TYPE_INTERFACE);
                    builder.decl_group = decl_group_of(me, decl, node);
                    builder_add_interface(me, &builder, &interface_decl->type.interface, ctx, decl);
                    builder.decl_group = nullptr;
                } else if (node->kind == SYMBOL_DECL_CLASS) {
                    builder_add_class_items(me, &builder, node->node, false, ctx, decl);
                }
            }
            builder_add_assignment_members(me, &builder, decl);
            for (SymbolDecl const* node = decl->decls; node < decl->decls + decl->decl_count; node++) {
                if (node->kind == SYMBOL_DECL_CLASS) {
                    Ty const* base = nullptr;
                    Symbol const* base_decl = nullptr;
                    if (class_base(me, node->node, ctx, &base, &base_decl) && base != ty) {
                        builder_add_inherited(me, &builder, this_param ? ty_with_this_arg(me, base, this_param) : base);
                    }
                } else if (node->kind == SYMBOL_DECL_INTERFACE) {
                    InterfaceDecl const* interface_decl = node->node;
                    for_each (extends, vec_spanned_type, &interface_decl->extends) {
                        Ty const* base = ty_from_node(me, &extends->type, ctx);
                        // An object type or an intersection of them (tsc's isValidBaseType)
                        if (base != ty && (base->flags & (TYF_OBJECT | TYF_INTERSECTION))) {
                            builder_add_inherited(me, &builder, this_param ? ty_with_this_arg(me, base, this_param) : base);
                        }
                    }
                }
            }
        }   break;

        case TY_DECLARED_CLASS_STATIC:
            for (SymbolDecl const* node = decl->decls; node < decl->decls + decl->decl_count; node++) {
                if (node->kind != SYMBOL_DECL_CLASS) continue;
                builder_add_class_static_side(me, &builder, ty, node->node);
                // `var C = class {}; C.x = 1`
                Symbol const* host = ((ClassDecl const*)node->node)->expando_host;
                if (host && host != decl) {
                    Members const* expandos = ty_members(me, ty_namespace_type(me, host));
                    for (u32 i = 0; i < expandos->prop_count; i++) {
                        if (!builder_find_prop(&builder, expandos->props[i]->name)) builder_push(&builder, props, expandos->props[i]);
                    }
                }
            }
            break;

        case TY_DECLARED_ENUM_OBJECT:
            for (SymbolDecl const* node = decl->decls; node < decl->decls + decl->decl_count; node++) {
                if (node->kind == SYMBOL_DECL_ENUM) builder_add_enum_object(me, &builder, node->node);
            }
            break;

        case TY_DECLARED_NAMESPACE:
            // The exported values, in declaration order
            u32 export_count;
            SymbolEntry* exports = symbol_collect_exports(ty->declared.namespace_symbol, &export_count);
            for (u32 i = 0; i < export_count; i++) {
                // `typeof globalThis` has the global `var`s and functions, not let, const, classes and enums
                if (ty->declared.namespace_symbol->is_global_this && (exports[i].symbol->flags & GLOBAL_BLOCK_SCOPED)) continue;
                // A type-only export (`export type { A }`, through `export type *`) isn't a value of the namespace (tsc's
                // isNamedMember: symbolIsValue, with getSymbolFlags excluding type-only meanings)
                // (`typeof ns.A` still names it: ty_namespace_type_only_prop())
                if (namespace_export_is_type_only(ty->declared.namespace_symbol, &exports[i])) continue;
                Prop* prop = namespace_export_prop(me, &exports[i]);
                if (prop) builder_push(&builder, props, prop);
            }
            free(exports);
            break;

        case TY_DECLARED_TUPLE_TARGET:
            // (Only the empty tuple is used as it is, the others are referenced with their elements)
            builder_finish(me, &builder);
            return resolve_tuple_members(me, ty);
        case TY_DECLARED_SYNTHETIC:
            break;
    }
    return builder_finish(me, &builder);
}

// `this_only`: the mapper only says what `this` is, so members that can't mention it are shared rather than
// instantiated (tsc's createInstantiatedSymbolTable with mappingThisOnly)
static MappedInfo const* instantiate_mapped_info(Checker* me, MappedInfo const* source, TyMapper const* mapper) {
    MappedInfo* mapped = arena_new(&my arena, MappedInfo);
    *mapped = *source;
    mapped->constraint = ty_instantiate(me, mapped->constraint, mapper);
    // The template and the `as` clause see the mapped type's own key parameter, not a mapping of it that the mapper
    // carries from an enclosing instance of the same mapped type: in `type X<T> = { [K in keyof T]: F<T[K]> }` where
    // F's body mentions X again, the inner X's mapper is composed from the outer one's, K included (tsc's
    // instantiateMappedType maps K to a fresh type parameter first, which shadows it)
    mapper = ty_mapper_without(me, mapper, (TyList){ .data = &source->type_param, .len = 1 });
    if (mapped->name_type) mapped->name_type = ty_instantiate(me, mapped->name_type, mapper);
    mapped->template_ = nullptr;
    mapped->template_node = nullptr;
    mapped->template_from = source;
    mapped->template_mapper = mapper;
    mapped->template_state = LAZY_STATE_NOT_STARTED;
    if (mapped->modifiers_type) mapped->modifiers_type = ty_instantiate(me, mapped->modifiers_type, mapper);
    return mapped;
}

static Members const* instantiate_members(
    Checker* me, Members const* source, TyMapper const* mapper, bool applies_type_args, bool this_only
) {
    MembersBuilder builder = {0};
    for (u32 i = 0; i < source->prop_count; i++) {
        if (this_only && source->props[i]->thisless) {
            builder_push(&builder, props, source->props[i]);
            continue;
        }
        Prop* prop = arena_new(&my arena, Prop);
        *prop = (Prop){
            .name = source->props[i]->name,
            .flags = source->props[i]->flags,
            .parent = source->props[i]->parent,
            .target = source->props[i],
            .mapper = mapper,
        };
        builder_push(&builder, props, prop);
    }
    for (u32 pass = 0; pass < 2; pass++) {
        u32 count = pass ? source->construct_count : source->call_count;
        Signature* const* sigs = pass ? source->construct : source->call;
        for (u32 i = 0; i < count; i++) {
            Signature* sig = arena_new(&my arena, Signature);
            *sig = (Signature){
                .flags = sigs[i]->flags, .target = sigs[i], .mapper = mapper, .drops_mapped_type_params = applies_type_args,
                .name_span = sigs[i]->name_span, .decl_span = sigs[i]->decl_span,
            };
            if (pass) {
                builder_push(&builder, construct, sig);
            } else {
                builder_push(&builder, call, sig);
            }
        }
    }
    for (u32 i = 0; i < source->index_count; i++) {
        IndexInfo info = source->index[i];
        info.key = ty_instantiate(me, info.key, mapper);
        info.value = ty_instantiate(me, info.value, mapper);
        builder_push(&builder, index, info);
    }
    if (source->mapped) builder.mapped = instantiate_mapped_info(me, source->mapped, mapper);
    Members* ret = (Members*)builder_finish(me, &builder);
    if (source->expanded_from) ret->expanded_from = instantiate_mapped_info(me, source->expanded_from, mapper);
    return ret;
}

static Members const* resolve_instantiated_members(Checker* me, Ty const* target, TyMapper const* mapper, bool applies_type_args) {
    return instantiate_members(me, ty_members(me, target), mapper, applies_type_args, false);
}

// The raw members of a declared type with a `this` type (see resolve_declared_members()), memoised in Ty.declared.raw
static Members const* declared_raw_members(Checker* me, Ty const* declared) {
    TyObjectState* state = declared->declared.raw;
    switch (state->members_state) {
        case LAZY_STATE_DONE: return state->members;
        case LAZY_STATE_IN_FLIGHT: return &empty_members; // `interface A extends A`
        case LAZY_STATE_NOT_STARTED: break;
    }
    state->members_state = LAZY_STATE_IN_FLIGHT;
    state->members = resolve_declared_members(me, declared);
    state->members_state = LAZY_STATE_DONE;
    return state->members;
}

// The members of `target` (a declared type with a `this` type) applied to type arguments and a this-argument (tsc's
// resolveTypeReferenceMembers)
static Members const* resolve_members_with_this(Checker* me, Ty const* target, TyList args, Ty const* this_arg) {
    TyList params = target->declared.type_params;
    Ty const** sources = arena_new_n(&my arena, Ty const*, params.len + 1);
    Ty const** targets = arena_new_n(&my arena, Ty const*, params.len + 1);
    bool this_only = true;
    for (u32 i = 0; i < params.len; i++) {
        sources[i] = params.data[i];
        targets[i] = args.data[i];
        this_only &= args.data[i] == params.data[i];
    }
    sources[params.len] = target->declared.this_param;
    targets[params.len] = this_arg;
    TyMapper const* mapper = ty_mapper_new(
        me, (TyList){ .data = sources, .len = params.len + 1 }, (TyList){ .data = targets, .len = params.len + 1 }
    );
    return instantiate_members(me, declared_raw_members(me, target), mapper, false, this_only);
}

// The property names a mapped type key produces: the key itself, or the `as` clause instantiated for it
static Ty const* mapped_key_name(Checker* me, MappedInfo const* mapped, Ty const* key) {
    if (!mapped->name_type) return key;
    TyMapper const* mapper = ty_mapper_new(
        me, (TyList){ .data = &mapped->type_param, .len = 1 }, (TyList){ .data = &key, .len = 1 }
    );
    return ty_instantiate(me, mapped->name_type, mapper);
}

// The members of a union, or `*ty` itself, or nothing for `never`. Points into `ty`, which must outlive the result
static void union_members(Ty const* const* ty_ref, Ty const* const** out, u32* count) {
    Ty const* ty = *ty_ref;
    *out = ty_ref;
    *count = 1;
    if (ty->kind == TY_UNION) {
        *out = ty->composite.types.data;
        *count = ty->composite.types.len;
    } else if (ty->flags & TYF_NEVER) {
        *count = 0;
    }
}

// `{ [K in keyof T]: X }` with T generic stays a mapped type, but has a property per property of T's apparent type (its
// constraint), X with K the property's name, with T's modifiers (tsc's resolveMappedTypeMembers, whose modifiers type
// is the apparent type): `Mutable<T>` with `T extends Node` has `pos: T["pos"]`
// Under exactOptionalPropertyTypes, what `T[K]` reads of an optional property of T adds is tsc's missing type, which
// an optional property of the mapped type doesn't have in its declared-type view (getNonMissingTypeOfSymbol): HACK,
// without a missing type the undefined is taken out (PROP_STRIP_UNDEFINED), even one the property declares (tsc
// keeps that one: `x?: string | undefined`). Not looked at here: forcing the property's type costs valibot 4%
static u32 exact_missing_flag(u32 flags, u32 from_flags) {
    if (!compiler_options()->exact_optional_property_types || !compiler_options()->strict_null_checks) return 0;
    return (flags & PROP_OPTIONAL) && (from_flags & PROP_OPTIONAL) ? PROP_STRIP_UNDEFINED : 0;
}

static Members const* expand_generic_intersection_homomorphic(Checker* me, Members const* members);

static Members const* expand_generic_homomorphic(Checker* me, Members const* members) {
    MappedInfo const* mapped = members->mapped;
    if (!mapped->keyof_constraint || mapped->name_type || !mapped->modifiers_type) return members;
    if (mapped->modifiers_type->kind == TY_INTERSECTION) return expand_generic_intersection_homomorphic(me, members);
    Ty const* apparent = ty_apparent_type(me, mapped->modifiers_type);
    if (apparent == mapped->modifiers_type || !(apparent->flags & TYF_OBJECT) || (apparent->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) {
        return members;
    }
    Members const* source = ty_members(me, apparent);
    if (source->mapped || (!source->prop_count && !source->index_count)) return members;
    Prop* template = arena_new(&my arena, Prop);
    template->type = ty_mapped_template(me, mapped);
    template->type_state = LAZY_STATE_DONE;
    MembersBuilder builder = { .mapped = mapped };
    for (u32 i = 0; i < source->prop_count; i++) {
        Prop const* from = source->props[i];
        Ty const* key = ty_prop_key_literal(me, from);
        TyMapper const* mapper = ty_mapper_new(
            me, (TyList){ .data = &mapped->type_param, .len = 1 }, (TyList){ .data = &key, .len = 1 }
        );
        u32 flags = from->flags & (PROP_OPTIONAL | PROP_READONLY);
        u32 from_flags = flags;
        if (mapped->optional > 0) flags |= PROP_OPTIONAL;
        if (mapped->optional < 0 && (flags & PROP_OPTIONAL)) {
            flags &= ~(u32)PROP_OPTIONAL;
            if (compiler_options()->strict_null_checks) flags |= PROP_STRIP_UNDEFINED;
        }
        flags |= exact_missing_flag(flags, from_flags);
        if (mapped->readonly > 0) flags |= PROP_READONLY;
        if (mapped->readonly < 0) flags &= ~(u32)PROP_READONLY;
        Prop* prop = arena_new(&my arena, Prop);
        *prop = (Prop){ .name = from->name, .flags = flags, .target = template, .mapper = mapper };
        Prop const* declared = from;
        while (declared && declared->target && !declared->decl_span_count) declared = declared->target;
        if (declared) {
            prop->name_span = declared->name_span;
            prop->decl_spans = declared->decl_spans;
            prop->decl_span_count = declared->decl_span_count;
            prop->name_is_value = declared->name_is_value;
        }
        builder_push(&builder, props, prop);
    }
    // And an index signature per index signature of the constraint (tsc's addMemberForKeyTypeWorker)
    for (u32 i = 0; i < source->index_count; i++) {
        IndexInfo const* from = &source->index[i];
        TyMapper const* mapper = ty_mapper_new(
            me, (TyList){ .data = &mapped->type_param, .len = 1 }, (TyList){ .data = &from->key, .len = 1 }
        );
        Ty const* value = ty_instantiate(me, ty_mapped_template(me, mapped), mapper);
        if (mapped->optional > 0 && compiler_options()->strict_null_checks) {
            Ty const* parts[2] = { value, ty_intrinsic(me, TY_UNDEFINED) };
            value = ty_union(me, parts, 2, nullptr, (TyList){0});
        }
        IndexInfo info = {
            .key = from->key,
            .value = value,
            .param_name = checker_atom(me, c_sv("x")),
            .readonly = mapped->readonly > 0 || (!mapped->readonly && from->readonly),
        };
        builder_push(&builder, index, info);
    }
    return builder_finish(me, &builder);
}

// The same over a generic intersection (`flatten<A<T> & B<T>>`): a property per property some part is known to have (a
// generic mapped part has those of the lower bound of its keys), optional if it is in every part that has it (tsc's
// resolveMappedTypeMembers over getPropertiesOfType of the intersection). No index signatures
static Members const* expand_generic_intersection_homomorphic(Checker* me, Members const* members) {
    MappedInfo const* mapped = members->mapped;
    TyList parts = mapped->modifiers_type->composite.types;
    Prop* template = nullptr;
    MembersBuilder builder = { .mapped = mapped };
    for (u32 p = 0; p < parts.len; p++) {
        Ty const* part = parts.data[p];
        if (!(part->flags & TYF_OBJECT)) part = ty_apparent_type(me, part);
        if (!(part->flags & TYF_OBJECT)) continue;
        Members const* source = ty_members(me, part);
        for (u32 i = 0; i < source->prop_count; i++) {
            Prop const* from = source->props[i];
            if (builder_find_prop(&builder, from->name)) continue;
            // Optional only if every part that has it has it optional, readonly if any has it readonly
            u32 flags = from->flags & (PROP_OPTIONAL | PROP_READONLY);
            for (u32 q = p + 1; q < parts.len; q++) {
                Ty const* other = parts.data[q];
                if (!(other->flags & TYF_OBJECT)) other = ty_apparent_type(me, other);
                Prop const* also = (other->flags & TYF_OBJECT) ? ty_get_prop(me, other, from->name) : nullptr;
                if (!also) continue;
                if (!(also->flags & PROP_OPTIONAL)) flags &= ~(u32)PROP_OPTIONAL;
                flags |= also->flags & PROP_READONLY;
            }
            u32 from_flags = flags;
            if (mapped->optional > 0) flags |= PROP_OPTIONAL;
            if (mapped->optional < 0 && (flags & PROP_OPTIONAL)) {
                flags &= ~(u32)PROP_OPTIONAL;
                if (compiler_options()->strict_null_checks) flags |= PROP_STRIP_UNDEFINED;
            }
            flags |= exact_missing_flag(flags, from_flags);
            if (mapped->readonly > 0) flags |= PROP_READONLY;
            if (mapped->readonly < 0) flags &= ~(u32)PROP_READONLY;
            if (!template) {
                template = arena_new(&my arena, Prop);
                template->type = ty_mapped_template(me, mapped);
                template->type_state = LAZY_STATE_DONE;
            }
            Ty const* key = ty_prop_key_literal(me, from);
            TyMapper const* mapper = ty_mapper_new(
                me, (TyList){ .data = &mapped->type_param, .len = 1 }, (TyList){ .data = &key, .len = 1 }
            );
            Prop* prop = arena_new(&my arena, Prop);
            *prop = (Prop){ .name = from->name, .flags = flags, .target = template, .mapper = mapper };
            Prop const* declared = from;
            while (declared && declared->target && !declared->decl_span_count) declared = declared->target;
            if (declared) {
                prop->name_span = declared->name_span;
                prop->decl_spans = declared->decl_spans;
                prop->decl_span_count = declared->decl_span_count;
                prop->name_is_value = declared->name_is_value;
            }
            builder_push(&builder, props, prop);
        }
    }
    return builder.props_count ? builder_finish(me, &builder) : members;
}

bool ty_is_valid_index_key(Ty const* ty) {
    if (ty->kind == TY_INTRINSIC && (ty->flags & (TYF_STRING | TYF_NUMBER | TYF_ES_SYMBOL))) return true;
    if (ty_is_pattern_literal(ty)) return true;
    if (ty->kind != TY_INTERSECTION || ty_is_generic_index(ty)) return false;
    for (u32 i = 0; i < ty->composite.types.len; i++) {
        if (ty_is_valid_index_key(ty->composite.types.data[i])) return true;
    }
    return false;
}

// `string & Tag`: a key of an index signature of its own (tsc's isValidIndexKeyType)
static bool is_index_key_intersection(Ty const* ty) {
    if (ty->kind != TY_INTERSECTION || ty_is_generic_index(ty)) return false;
    for (u32 i = 0; i < ty->composite.types.len; i++) {
        Ty const* member = ty->composite.types.data[i];
        if (member->kind == TY_INTRINSIC && (member->flags & (TYF_STRING | TYF_NUMBER | TYF_ES_SYMBOL))) return true;
        if (ty_is_pattern_literal(member)) return true;
    }
    return false;
}

// The property of a mapped type's modifiers type that a key names, and its optional/readonly flags. Of a union or
// intersection like tsc's createUnionOrIntersectionProperty: a union's is optional if it is in any member (none if a
// member lacks it), an intersection's only if it is in every member that has it; readonly if any is
// Whether the modifiers type's index signature for a key type is readonly (tsc's getApplicableIndexInfo(modifiersType,
// propNameType)?.isReadonly)
static bool modifiers_index_readonly(Checker* me, Ty const* modifiers, Ty const* key) {
    if (modifiers->kind == TY_UNION) return false;
    IndexInfo const* info = ty_applicable_index_info(me, modifiers, key);
    return info && info->readonly;
}

static Prop const* modifiers_prop(Checker* me, Ty const* modifiers, Atom const* name, u32* flags) {
    *flags = 0;
    if (modifiers->kind != TY_UNION && modifiers->kind != TY_INTERSECTION) {
        Prop const* prop = ty_get_prop(me, modifiers, name);
        if (prop) *flags = prop->flags & (PROP_OPTIONAL | PROP_READONLY);
        return prop;
    }
    bool is_union = modifiers->kind == TY_UNION;
    Prop const* first = nullptr;
    // (Like tsc's createUnionOrIntersectionProperty: a union's property is optional or readonly if any constituent's is,
    // an intersection's only if every one that has it is)
    u32 optional = is_union ? 0 : PROP_OPTIONAL, readonly = is_union ? 0 : PROP_READONLY;
    for (u32 i = 0; i < modifiers->composite.types.len; i++) {
        Ty const* member = ty_apparent_type(me, modifiers->composite.types.data[i]);
        u32 member_flags = 0;
        Prop const* prop = (member->flags & TYF_OBJECT) || member->kind == TY_UNION || member->kind == TY_INTERSECTION ?
            modifiers_prop(me, member, name, &member_flags) : nullptr;
        if (!prop) {
            if (is_union) return nullptr;
            continue;
        }
        if (!first) first = prop;
        if (is_union) optional |= member_flags & PROP_OPTIONAL; else optional &= member_flags;
        if (is_union) readonly |= member_flags & PROP_READONLY; else readonly &= member_flags;
    }
    if (first) *flags = (optional & PROP_OPTIONAL) | readonly;
    return first;
}

// What a generic key type is known to include at least (tsc's getLowerBoundOfKeyType): `keyof T` the keys of T's
// constraint, a distributive conditional type over it (`Exclude<keyof T, K>`) that conditional type of those keys
static Ty const* lower_bound_of_key_type(Checker* me, Ty const* ty, u32 depth) {
    if (depth > 8) return ty;
    if (ty->kind == TY_KEYOF) {
        Ty const* apparent = ty_apparent_type(me, ty->keyof.operand);
        // (TODO: tsc takes the known keys of a generic tuple, getKnownKeysOfTupleType)
        if (apparent == ty->keyof.operand || ty_is_generic_tuple(me, apparent)) return ty;
        return ty_keyof(me, apparent);
    }
    if (ty->kind == TY_CONDITIONAL) {
        Ty const* root = ty->conditional.root ? ty->conditional.root : ty;
        if (!ty->conditional.distributive || root->conditional.check->kind != TY_TYPE_PARAM) return ty;
        Ty const* check = ty->conditional.check;
        Ty const* lower = lower_bound_of_key_type(me, check, depth + 1);
        if (lower == check) return ty;
        Ty const* param = root->conditional.check;
        TyMapper const* mapper = ty->conditional.mapper ? ty_mapper_with(me, ty->conditional.mapper, param, lower) :
            ty_mapper_new(me, (TyList){ .data = &param, .len = 1 }, (TyList){ .data = &lower, .len = 1 });
        return ty_instantiate(me, root, mapper);
    }
    if (ty->kind == TY_UNION || ty->kind == TY_INTERSECTION) {
        TyList types = ty->composite.types;
        if (ty->kind == TY_INTERSECTION && types.len == 2 && (types.data[0]->flags & (TYF_STRING | TYF_NUMBER | TYF_BIGINT)) &&
            ty_is_empty_anonymous_object(me, types.data[1])
        ) {
            return ty;
        }
        TyBuf parts = {0};
        bool changed = false;
        for (u32 i = 0; i < types.len; i++) {
            Ty const* part = lower_bound_of_key_type(me, types.data[i], depth + 1);
            changed |= part != types.data[i];
            ty_buf_push(&parts, part);
        }
        Ty const* ret = !changed ? ty : ty->kind == TY_UNION ? ty_union(me, parts.data, parts.len, nullptr, (TyList){0}) :
            ty_intersection(me, parts.data, parts.len, nullptr, (TyList){0});
        ty_buf_drop(&parts);
        return ret;
    }
    return ty;
}

// A mapped type whose key type is known has ordinary members: a property per literal key (or per literal the `as`
// clause maps a key to), an index signature per primitive key. Returns the members unchanged while the key type is
// generic (or something this can't expand yet)
static Members const* expand_mapped(Checker* me, Ty const* ty, Members const* members) {
    MappedInfo const* mapped = members->mapped;
    if (!mapped) return members;
    Ty const* constraint = mapped->constraint;
    // A generic key type that is known to include some keys (`Pick<T, Exclude<keyof T, K>>`, as Omit is): a property
    // per such key, but the mapped type stays generic (tsc's resolveMappedTypeMembers, which adds members for the
    // lower bound of the key type)
    bool generic = false;
    if (ty_is_generic_index(constraint)) {
        if (mapped->keyof_constraint) return expand_generic_homomorphic(me, members);
        Ty const* lower = lower_bound_of_key_type(me, constraint, 0);
        if (lower == constraint) return members;
        constraint = lower;
        generic = true;
    }
    Ty const* const* keys;
    u32 key_count;
    union_members(&constraint, &keys, &key_count);
    Ty const** member_keys = nullptr;
    Ty const* keyed = mapped->modifiers_type;
    bool keyed_intersection = keyed && keyed->kind == TY_INTERSECTION && !(keyed->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES);
    if (mapped->keyof_constraint && keyed && ((keyed->flags & TYF_OBJECT) || keyed_intersection)) {
        // `[K in keyof T]` goes by the members of T (tsc's forEachMappedTypePropertyKeyTypeAndIndexSignatureKeyType):
        // a string index is the key `string` alone, and the property names aren't absorbed by it like in `keyof T`.
        // Of an intersection, each member's (tsc's are the intersection's properties and index infos)
        TyList parts = keyed_intersection ? keyed->composite.types : (TyList){ .data = &keyed, .len = 1 };
        u32 cap = 0;
        for (u32 p = 0; p < parts.len; p++) {
            Ty const* part = ty_apparent_type(me, parts.data[p]);
            if (!(part->flags & TYF_OBJECT)) continue;
            Members const* source = ty_members(me, part);
            cap += source->prop_count + source->index_count;
        }
        member_keys = arena_new_n(&my arena, Ty const*, cap ? cap : 1);
        key_count = 0;
        for (u32 p = 0; p < parts.len; p++) {
            Ty const* part = ty_apparent_type(me, parts.data[p]);
            if (!(part->flags & TYF_OBJECT)) continue;
            Members const* source = ty_members(me, part);
            for (u32 i = 0; i < source->prop_count + source->index_count; i++) {
                Ty const* key = i < source->prop_count ? ty_prop_key_literal(me, source->props[i]) :
                    source->index[i - source->prop_count].key;
                bool seen = false;
                for (u32 j = 0; j < key_count && !seen; j++) seen = member_keys[j] == key;
                if (!seen) member_keys[key_count++] = key;
            }
        }
        keys = member_keys;
    }
    if (mapped->name_type) {
        // A mapped type whose `as` clause is generic in more than its key is generic (tsc's isGenericMappedType):
        // `{ [K in keyof M as `${K}${Suffix}`]: ... }`
        Ty const* names = mapped_key_name(me, mapped, constraint);
        Ty const* const* parts;
        u32 part_count;
        union_members(&names, &parts, &part_count);
        for (u32 i = 0; i < part_count; i++) {
            if (parts[i]->kind == TY_TYPE_PARAM || (parts[i]->flags & TYF_DEFERRED)) return members;
        }
    }
    Ty const* modifiers = mapped->modifiers_type;
    if (generic && modifiers) modifiers = ty_apparent_type(me, modifiers);
    if (modifiers && !(modifiers->flags & TYF_OBJECT) && modifiers->kind != TY_UNION && modifiers->kind != TY_INTERSECTION) {
        modifiers = nullptr;
    }
    // The template as a property to instantiate per key (lowered when a property's type is first asked for, like tsc's
    // getTypeOfMappedSymbol: the members can be there before the template can be lowered)
    Prop* template = arena_new(&my arena, Prop);
    template->template_of = mapped;

    MembersBuilder builder = { .mapped = generic ? mapped : nullptr };
    for (u32 i = 0; i < key_count; i++) {
        Ty const* key = keys[i];
        TyMapper const* mapper = ty_mapper_new(
            me, (TyList){ .data = &mapped->type_param, .len = 1 }, (TyList){ .data = &key, .len = 1 }
        );
        Ty const* name_type = mapped_key_name(me, mapped, key);
        Ty const* const* names;
        u32 name_count;
        union_members(&name_type, &names, &name_count);
        for (u32 j = 0; j < name_count; j++) {
            Ty const* prop_name = names[j];
            if (prop_name->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL | TYF_UNIQUE_ES_SYMBOL)) {
                Atom const* name = ty_literal_name(me, prop_name);
                Prop* existing = builder_find_prop(&builder, name);
                if (existing) {
                    // Two keys with one name (members of different enums with the same value, or an `as` clause
                    // mapping several keys to one name): the property is about both
                    Ty const* both[2] = { existing->mapper->targets[0], key };
                    Ty const* merged = ty_union(me, both, 2, nullptr, (TyList){0});
                    existing->mapper = ty_mapper_new(
                        me, (TyList){ .data = &mapped->type_param, .len = 1 }, (TyList){ .data = &merged, .len = 1 }
                    );
                    continue;
                }
                // Modifiers come from the property the key (not the name it's mapped to) names
                Prop const* from = nullptr;
                u32 flags = 0;
                if (modifiers && (key->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL | TYF_UNIQUE_ES_SYMBOL))) {
                    from = modifiers_prop(me, modifiers, ty_literal_name(me, key), &flags);
                }
                u32 from_flags = flags;
                if (mapped->optional > 0) flags |= PROP_OPTIONAL;
                if (mapped->optional < 0 && (flags & PROP_OPTIONAL)) {
                    flags &= ~(u32)PROP_OPTIONAL;
                    if (compiler_options()->strict_null_checks) flags |= PROP_STRIP_UNDEFINED;
                }
                flags |= exact_missing_flag(flags, from_flags);
                if (mapped->readonly > 0) flags |= PROP_READONLY;
                if (mapped->readonly < 0) flags &= ~(u32)PROP_READONLY;
                Prop* prop = arena_new(&my arena, Prop);
                *prop = (Prop){
                    .name = name,
                    .flags = flags,
                    .target = template,
                    .mapper = mapper,
                    .mapped_type = ty,
                };
                // A fresh symbol, declared where the property it comes from is (tsc's resolveMappedTypeMembers), unless
                // the `as` clause renamed it (tsc: a remapping rather than a filtering `as` clause; approximated per key)
                Prop const* declared = prop_name == key ? from : nullptr;
                while (declared && declared->target && !declared->decl_span_count) declared = declared->target;
                if (declared) {
                    prop->name_span = declared->name_span;
                    prop->decl_spans = declared->decl_spans;
                    prop->decl_span_count = declared->decl_span_count;
                    prop->name_is_value = declared->name_is_value;
                }
                builder_push(&builder, props, prop);
            } else {
                // Primitives and patterns (`data-${string}`) are index signatures, what else a name can be (generic,
                // a symbol, an object) makes nothing (tsc's addMemberForKeyTypeWorker)
                if (prop_name->flags & (TYF_ANY | TYF_STRING)) {
                    prop_name = ty_intrinsic(me, TY_STRING);
                } else if (prop_name->flags & (TYF_NUMBER | TYF_ENUM)) {
                    prop_name = ty_intrinsic(me, TY_NUMBER);
                } else if (!(prop_name->flags & TYF_ES_SYMBOL) && !ty_is_pattern_literal(prop_name) &&
                    !is_index_key_intersection(prop_name)
                ) {
                    continue;
                }
                Ty const* value = ty_instantiate(me, ty_mapped_template(me, mapped), mapper);
                // `?` makes the index signature's values possibly undefined, even with exactOptionalPropertyTypes
                // (tsc's getTemplateTypeFromMappedType adds optionality to the template: its missing type prints and
                // relates as undefined in an index signature)
                if (mapped->optional > 0 && compiler_options()->strict_null_checks) {
                    Ty const* parts[2] = { value, ty_intrinsic(me, TY_UNDEFINED) };
                    value = ty_union(me, parts, 2, nullptr, (TyList){0});
                }
                IndexInfo* existing = nullptr;
                for (u32 k = 0; k < builder.index_count; k++) {
                    if (builder.index[k].key == prop_name) existing = &builder.index[k];
                }
                if (existing) {
                    Ty const* both[2] = { existing->value, value };
                    existing->value = ty_union(me, both, 2, nullptr, (TyList){0});
                    continue;
                }
                IndexInfo info = {
                    .key = prop_name,
                    .value = value,
                    .param_name = checker_atom(me, c_sv("x")),
                    // (Without a modifier, as readonly as the index signature the modifiers type has for the key)
                    .readonly = mapped->readonly > 0 || (!mapped->readonly && modifiers && modifiers_index_readonly(me, modifiers, prop_name)),
                };
                builder_push(&builder, index, info);
            }
        }
    }
    Members* ret = (Members*)builder_finish(me, &builder);
    if (!generic) ret->expanded_from = mapped;
    return ret;
}

static Members const* resolve_members_inner(Checker* me, Ty const* ty);

// A tuple's members (tsc's createTupleTargetType): a property per element before any rest element, a readonly
// `length` (the possible lengths, or number with a rest element), then what the array of its elements has
static Members const* resolve_tuple_members(Checker* me, Ty const* tuple) {
    Ty const* target = tuple->kind == TY_REFERENCE ? tuple->reference.target : tuple;
    TupleShape const* shape = target->declared.tuple;
    TyList elements = tuple->kind == TY_REFERENCE ? ty_reference_args(me, tuple) : target->declared.type_params;
    MembersBuilder builder = {0};
    u32 min_length = 0;
    bool variable = false;
    for (u32 i = 0; i < shape->arity; i++) {
        if (shape->flags[i] & TUPLE_ELEMENT_REST) {
            variable = true;
            break;
        }
        bool optional = (shape->flags[i] & TUPLE_ELEMENT_OPTIONAL) != 0;
        if (!optional) min_length = i + 1;
        cstr name = cstr_d(i);
        Prop* prop = arena_new(&my arena, Prop);
        prop->name = checker_atom(me, cstr_sv(&name));
        cstr_drop(&name);
        prop->flags = (optional ? PROP_OPTIONAL : 0) | (shape->readonly ? PROP_READONLY : 0);
        prop->type = elements.data[i];
        prop->type_state = LAZY_STATE_DONE;
        prop->name_is_value = true;
        builder_push(&builder, props, prop);
    }
    Ty const* length;
    if (variable) {
        length = ty_intrinsic(me, TY_NUMBER);
    } else {
        u32 count = shape->arity - min_length + 1;
        Ty const** lengths = malloc(sizeof(Ty const*) * count);
        panic_if(!lengths, "out of memory");
        for (u32 i = 0; i < count; i++) {
            Value value = value_number(min_length + i);
            lengths[i] = ty_literal(me, &value);
        }
        length = ty_union(me, lengths, count, nullptr, (TyList){0});
        free(lengths);
    }
    Prop* length_prop = arena_new(&my arena, Prop);
    length_prop->name = checker_atom(me, c_sv("length"));
    length_prop->flags = shape->readonly ? PROP_READONLY : 0; // Like tsc's, a mutable tuple's length can be assigned
    length_prop->type = length;
    length_prop->type_state = LAZY_STATE_DONE;
    builder_push(&builder, props, length_prop);
    Ty const* array = ty_tuple_as_array(me, tuple);
    if (array) builder_add_inherited(me, &builder, array);
    return builder_finish(me, &builder);
}

static Members const* resolve_intersection_members(Checker* me, Ty const* ty);

static Members const* resolve_members(Checker* me, Ty const* ty) {
    if (ty->kind == TY_INTERSECTION) return resolve_intersection_members(me, ty);
    return expand_mapped(me, ty, resolve_members_inner(me, ty));
}

static Members const* resolve_members_inner(Checker* me, Ty const* ty) {
    switch (ty->kind) {
        case TY_DECLARED:
            if (ty->declared.this_param) {
                // As a type of its own it is the class/interface applied to its own type parameters, `this` included
                Ty const* self = ty->declared.type_params.len ? ty_reference(me, ty, ty->declared.type_params) : ty;
                return resolve_members_with_this(me, ty, ty->declared.type_params, self);
            }
            return resolve_declared_members(me, ty);

        case TY_REFERENCE: {
            Ty const* target = ty->reference.target;
            if (target->declared.kind == TY_DECLARED_TUPLE_TARGET) return resolve_tuple_members(me, ty);
            if (target->declared.this_param) {
                Ty const* this_arg = ty->reference.this_arg ? ty->reference.this_arg : ty;
                return resolve_members_with_this(me, target, ty_reference_args(me, ty), this_arg);
            }
            TyMapper const* mapper = ty_mapper_new(me, target->declared.type_params, ty_reference_args(me, ty));
            return resolve_instantiated_members(me, target, mapper, false);
        }

        case TY_ANONYMOUS: {
            if (ty->anonymous.target) {
                return resolve_instantiated_members(me, ty->anonymous.target, ty->anonymous.mapper, ty->anonymous.applies_type_args);
            }
            MembersBuilder builder = {0};
            if (ty->anonymous.merged_value) {
                builder_add_merged_value(me, &builder, ty->anonymous.merged_value);
            } else if (ty->anonymous.function) {
                FunctionImpl const* function = ty->anonymous.function;
                Signature* sig = signature_from_function(me, function, ty->anonymous.ctx);
                if (function->jsdoc_overload_count) {
                    // JSDoc `@overload`s instead of its own
                    for (u32 i = 0; i < function->jsdoc_overload_count; i++) {
                        builder_add_callable(me, &builder, function->jsdoc_overloads[i].callable->function, ty->anonymous.ctx);
                    }
                } else if (sig->flags & SIGNATURE_CONSTRUCT) {
                    builder_push(&builder, construct, sig);
                } else {
                    builder_push(&builder, call, sig);
                }
                // A JavaScript constructor function is also a class: `new` makes its instances (tsc's
                // resolveAnonymousTypeMembers, the construct signatures of a function with SymbolFlags.Class)
                Symbol const* js_class = ty_js_constructor_class(ty->anonymous.function);
                if (js_class && !(sig->flags & SIGNATURE_CONSTRUCT)) {
                    Signature* construct = signature_from_function(me, ty->anonymous.function, ty->anonymous.ctx);
                    construct->flags |= SIGNATURE_CONSTRUCT;
                    construct->class_instance = ty_class_instance_type(me, js_class);
                    construct->class_type_params = ty_local_type_params(ty_declared_type_of(me, js_class));
                    builder_push(&builder, construct, construct);
                }
                // `const f = () => {}; f.x = 1` (tsc merges the variable's exports into the function's symbol)
                Symbol const* host = ty->anonymous.function->expando_host;
                if (host) {
                    Members const* expandos = ty_members(me, ty_namespace_type(me, host));
                    for (u32 i = 0; i < expandos->prop_count; i++) builder_push(&builder, props, expandos->props[i]);
                }
            } else if (ty->anonymous.overload_count) {
                for (u32 i = 0; i < ty->anonymous.overload_count; i++) {
                    builder_add_callable(me, &builder, ty->anonymous.overloads[i], ty->anonymous.ctx);
                }
            } else if (ty->anonymous.node->tag == TYPE_CALLABLE) {
                builder_add_callable(me, &builder, ty->anonymous.node->callable->function, ty->anonymous.ctx);
            } else {
                check(ty->anonymous.node->tag == TYPE_INTERFACE);
                builder_add_interface(me, &builder, &ty->anonymous.node->interface, ty->anonymous.ctx, nullptr);
            }
            return builder_finish(me, &builder);
        }

        default: impossible;
    }
}

// The members of what an intersection member stands for (tsc's getSignaturesOfType/getIndexInfosOfType on it: its
// apparent type)
static Members const* part_members(Checker* me, Ty const* part) {
    Ty const* apparent = ty_apparent_type(me, part);
    return apparent->flags & (TYF_OBJECT | TYF_INTERSECTION) ? ty_members(me, apparent) : &empty_members;
}

// tsc's isMixinConstructorType: a single construct signature `new (...args: any[]) => X` (or `...args: any`) without
// type parameters
static bool is_mixin_constructor(Checker* me, Members const* members) {
    if (members->construct_count != 1) return false;
    Signature const* sig = members->construct[0];
    signature_resolve(me, sig);
    if (sig->type_params.len || sig->param_count != 1 || !sig->params[0].rest) return false;
    Ty const* type = sig->params[0].type;
    return (type->flags & TYF_ANY) || (ty_is_array(me, type) && (ty_reference_args(me, type).data[0]->flags & TYF_ANY));
}

// Adds a signature unless an identical one is there (tsc's appendSignatures)
static void builder_add_signature(Checker* me, MembersBuilder* builder, Signature* sig, bool construct) {
    Signature* const* list = construct ? builder->construct : builder->call;
    u32 count = construct ? builder->construct_count : builder->call_count;
    for (u32 i = 0; i < count; i++) {
        if (list[i] == sig || ty_signatures_identical(me, list[i], sig, false, false)) return;
    }
    if (construct) {
        builder_push(builder, construct, sig);
    } else {
        builder_push(builder, call, sig);
    }
}

// tsc's resolveIntersectionTypeMembers: no properties (those are found member by member), the members' call signatures,
// their construct signatures except those of mixin constructors, whose return types are intersected into every other
// construct signature's instead (`{ new (...args: any[]): A } & { new (s: string): B }` is `new (s: string) => A & B`;
// when every constructor is a mixin, the first stays one), index infos of the same key intersected
static Members const* resolve_intersection_members(Checker* me, Ty const* ty) {
    TyList types = ty->composite.types;
    Members const** parts = malloc(sizeof(Members const*) * types.len);
    bool* mixin = malloc(sizeof(bool) * types.len);
    panic_if(!parts || !mixin, "out of memory");
    u32 constructors = 0, mixins = 0;
    for (u32 i = 0; i < types.len; i++) {
        parts[i] = part_members(me, types.data[i]);
        mixin[i] = is_mixin_constructor(me, parts[i]);
        constructors += parts[i]->construct_count > 0;
        mixins += mixin[i];
    }
    if (constructors && constructors == mixins) {
        for (u32 i = 0; i < types.len; i++) {
            if (mixin[i]) {
                mixin[i] = false;
                mixins--;
                break;
            }
        }
    }
    MembersBuilder builder = {0};
    for (u32 i = 0; i < types.len; i++) {
        Members const* members = parts[i];
        if (!mixin[i]) {
            for (u32 j = 0; j < members->construct_count; j++) {
                Signature* sig = members->construct[j];
                if (mixins) {
                    // (tsc's includeMixinType: the mixins' instances, this one's in its place)
                    Ty const** mixed = malloc(sizeof(Ty const*) * types.len);
                    panic_if(!mixed, "out of memory");
                    u32 mixed_len = 0;
                    for (u32 k = 0; k < types.len; k++) {
                        if (k == i) {
                            mixed[mixed_len++] = signature_return_type(me, sig);
                        } else if (mixin[k]) {
                            mixed[mixed_len++] = signature_return_type(me, parts[k]->construct[0]);
                        }
                    }
                    Signature* clone = arena_new(&my arena, Signature);
                    signature_resolve(me, sig);
                    *clone = *sig;
                    clone->ret = ty_intersection(me, mixed, mixed_len, nullptr, (TyList){0});
                    clone->ret_state = LAZY_STATE_DONE;
                    free(mixed);
                    sig = clone;
                }
                builder_add_signature(me, &builder, sig, true);
            }
        }
        for (u32 j = 0; j < members->call_count; j++) builder_add_signature(me, &builder, members->call[j], false);
        for (u32 j = 0; j < members->index_count; j++) {
            IndexInfo info = members->index[j];
            bool merged = false;
            for (u32 k = 0; k < builder.index_count && !merged; k++) {
                IndexInfo* existing = &builder.index[k];
                if (existing->key != info.key) continue;
                Ty const* both[] = { existing->value, info.value };
                existing->value = ty_intersection(me, both, 2, nullptr, (TyList){0});
                existing->readonly = existing->readonly && info.readonly;
                existing->decl_span = span_null;
                existing->components = nullptr;
                existing->component_count = 0;
                merged = true;
            }
            if (!merged) builder_push(&builder, index, info);
        }
    }
    free(parts);
    free(mixin);
    return builder_finish(me, &builder);
}

static TyObjectState* object_state(Ty const* ty) {
    switch (ty->kind) {
        case TY_DECLARED: return ty->declared.state;
        case TY_REFERENCE: return ty->reference.state;
        case TY_ANONYMOUS: return ty->anonymous.state;
        case TY_INTERSECTION: return ty->composite.state;
        default: return nullptr;
    }
}

Members const* ty_members(Checker* me, Ty const* ty) {
    TyObjectState* state = object_state(ty);
    if (!state) return &empty_members;
    switch (state->members_state) {
        case LAZY_STATE_DONE:
            return state->members;

        case LAZY_STATE_IN_FLIGHT:
            // Only a type whose own members depend on its own members gets here (`interface A extends A`)
            return &empty_members;

        case LAZY_STATE_NOT_STARTED:
            state->members_state = LAZY_STATE_IN_FLIGHT;
            state->members = resolve_members(me, ty);
            state->members_state = LAZY_STATE_DONE;
            return state->members;
    }
    impossible;
}

Prop const* ty_get_prop(Checker* me, Ty const* ty, Atom const* name) {
    Members const* members = ty_members(me, ty);
    if (members->props_by_name) {
        u32 lo = 0, hi = members->prop_count;
        while (lo < hi) {
            u32 mid = lo + (hi - lo) / 2;
            Atom const* mid_name = members->props_by_name[mid]->name;
            if (mid_name == name) return members->props_by_name[mid];
            if ((uintptr_t)mid_name < (uintptr_t)name) lo = mid + 1; else hi = mid;
        }
        return nullptr;
    }
    for (u32 i = 0; i < members->prop_count; i++) {
        if (members->props[i]->name == name) return members->props[i];
    }
    return nullptr;
}

Span ty_prop_decl_span(Prop const* prop) {
    while (prop && !span_valid(prop->name_span)) prop = prop->target;
    if (!prop) return (Span){0};
    // An assignment declaration (`this.x = v`) is the whole access (tsc's getErrorSpanForNode of the declaration)
    Symbol const* symbol = prop->symbol;
    if (symbol && symbol->decl_count && symbol->decls[0].kind == SYMBOL_DECL_ASSIGNMENT && span_valid(symbol->decls[0].name) &&
        span_contains_span(symbol->decls[0].name, prop->name_span)
    ) {
        return symbol->decls[0].name;
    }
    return prop->name_span;
}

Ty const* prop_type(Checker* me, Prop const* const_prop) {
    Prop* prop = (Prop*)const_prop; // The lazily resolved type is the one mutable thing about a Prop
    switch (prop->type_state) {
        case LAZY_STATE_DONE: return prop->type;
        case LAZY_STATE_IN_FLIGHT:
            prop->circular = true;
            return ty_intrinsic(me, TY_CIRCULAR); // TODO: TS2502 and friends
        case LAZY_STATE_NOT_STARTED: break;
    }
    prop->type_state = LAZY_STATE_IN_FLIGHT;
    Ty const* type;
    if (prop->flags & (PROP_GETTER | PROP_SETTER) && !prop->target) {
        type = ty_object_literal_accessor_type(me, prop->function, prop->flags & PROP_GETTER);
    } else if (prop->target) {
        type = ty_instantiate(me, prop_type(me, prop->target), prop->mapper);
        if (prop->flags & PROP_STRIP_UNDEFINED) type = ty_without_undefined(me, type); // (Only set with strictNullChecks)
    } else if (prop->overload_count) {
        type = ty_anonymous_from_overloads(me, prop->overloads, prop->overload_count, prop->ctx);
    } else if (prop->symbol) {
        type = ty_type_of_symbol(me, prop->symbol);
    } else if (prop->function) {
        type = ty_function_type(me, prop->function);
    } else if (prop->lazy) {
        type = lazy_type_get((LazyType*)prop->lazy);
    } else if (prop->template_of) {
        type = ty_mapped_template(me, prop->template_of);
    } else {
        type = ty_from_node(me, prop->node, prop->ctx);
    }
    if (prop->circular && prop->mapped_type) {
        // tsc's getTypeOfMappedSymbol when popTypeResolution fails
        prop->type = ty_intrinsic(me, TY_ERROR);
        prop->type_state = LAZY_STATE_DONE;
        cstr name = ty_prop_name_cstr(me, prop->name);
        cstr mapped = ty_to_cstr(me, prop->mapped_type);
        ty_error_at_current_node_d(me, 2615, "Type of property '", name, "' circularly references itself in mapped type '",
            mapped, "'.");
        c_drop(cstr, &name, &mapped);
        return prop->type;
    }
    prop->type = type;
    prop->type_state = LAZY_STATE_DONE;
    return type;
}

Ty const* prop_write_type(Checker* me, Prop const* const_prop) {
    Prop* prop = (Prop*)const_prop; // Resolved lazily like the type
    if (prop->write_type) return prop->write_type;
    Ty const* type = nullptr;
    if (prop->write_node) {
        type = ty_from_node(me, prop->write_node, prop->ctx);
    } else if (prop->target) {
        type = prop_write_type(me, prop->target);
        if (type) type = ty_instantiate(me, type, prop->mapper);
    }
    prop->write_type = type;
    return type;
}

// A generic signature instantiated by an outer mapper (a method of `Box<number>`) gets fresh type parameters whose
// constraints and defaults are instantiated too (tsc's instantiateSignature). Type parameters that have neither are
// shared with the target, which is indistinguishable and cheaper, unless the mapper brings in type variables: `T := U`
// in the method `then<U>(cb: (value: T) => P<U>)` of `P<U>` would capture them. The ones the mapper replaces are being
// instantiated away (see ty_signature_instantiation()) and stay
static void clone_type_params(Checker* me, Signature* sig) {
    TyList params = sig->type_params;
    TyMapper const* outer = sig->mapper;
    bool any = false;
    for (u32 j = 0; j < outer->len && !any; j++) any = outer->targets[j]->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES;
    for (u32 i = 0; i < params.len && !any; i++) {
        bool mapped = false;
        for (u32 j = 0; j < outer->len; j++) mapped |= outer->sources[j] == params.data[i];
        any = !mapped && ty_param_is_bounded(params.data[i]);
    }
    if (!any) return;
    Ty const** fresh = arena_new_n(&my arena, Ty const*, params.len);
    Ty const** sources = arena_new_n(&my arena, Ty const*, outer->len + params.len);
    Ty const** targets = arena_new_n(&my arena, Ty const*, outer->len + params.len);
    memcpy(sources, outer->sources, sizeof(Ty const*) * outer->len);
    memcpy(targets, outer->targets, sizeof(Ty const*) * outer->len);
    u32 len = outer->len;
    for (u32 i = 0; i < params.len; i++) {
        bool mapped = false;
        for (u32 j = 0; j < outer->len; j++) mapped |= outer->sources[j] == params.data[i];
        fresh[i] = mapped ? params.data[i] : ty_clone_type_param(me, params.data[i], nullptr, nullptr);
        if (fresh[i] == params.data[i]) continue;
        sources[len] = params.data[i];
        targets[len] = fresh[i];
        len++;
    }
    TyMapper const* mapper = ty_mapper_new(me, (TyList){ .data = sources, .len = len }, (TyList){ .data = targets, .len = len });
    for (u32 i = 0; i < params.len; i++) {
        if (fresh[i] != params.data[i]) ((Ty*)fresh[i])->param.state->clone_mapper = mapper;
    }
    sig->mapper = mapper;
    sig->type_params = (TyList){ .data = fresh, .len = params.len };
}

Symbol const* ty_js_constructor_class(FunctionImpl const* function) {
    if (function->is_type || span_valid(function->arrow_span) || function->this_literal) return nullptr;
    Symbol const* symbol = function->class_symbol ? function->class_symbol : function->is_expression ? nullptr : function->symbol;
    if (!symbol || !(symbol->flags & SYMBOL_CLASS) || !symbol->members) return nullptr;
    SourceFile const* file = function->full_span.source;
    return file && file->is_js ? symbol : nullptr;
}

// tsc's IsUntypedSignatureInJSFile: a function of a JavaScript file without JSDoc parameter types (or `@type`), not
// called where it is made. Every argument is optional (getMinArgumentCount)
static bool function_is_untyped_js(FunctionImpl const* function) {
    SourceFile const* file = function->full_span.source;
    return file && file->is_js && !function->is_type && !function->is_iife && !function->jsdoc_typed;
}

void signature_resolve(Checker* me, Signature const* const_sig) {
    Signature* sig = (Signature*)const_sig;
    if (sig->state != LAZY_STATE_NOT_STARTED) return;
    sig->state = LAZY_STATE_IN_FLIGHT;
    if (sig->target) {
        Signature const* target = sig->target;
        signature_resolve(me, target);
        sig->type_params = target->type_params;
        if (sig->drops_mapped_type_params && sig->type_params.len) {
            Ty const** kept = arena_new_n(&my arena, Ty const*, sig->type_params.len);
            u32 kept_count = 0;
            for (u32 i = 0; i < target->type_params.len; i++) {
                bool mapped = false;
                for (u32 j = 0; j < sig->mapper->len; j++) mapped |= sig->mapper->sources[j] == target->type_params.data[i];
                if (!mapped) kept[kept_count++] = target->type_params.data[i];
            }
            sig->type_params = (TyList){ .data = kept, .len = kept_count };
        }
        if (sig->mapper) clone_type_params(me, sig);
        SigParam* params = arena_new_n(&my arena, SigParam, target->param_count);
        for (u32 i = 0; i < target->param_count; i++) {
            params[i] = target->params[i];
            params[i].type = ty_instantiate(me, params[i].type, sig->mapper);
        }
        sig->params = params;
        sig->param_count = target->param_count;
        sig->min_args = target->min_args;
        sig->this_type = target->this_type ? ty_instantiate(me, target->this_type, sig->mapper) : nullptr;
    }
    if (!sig->target && sig->function) {
        FunctionImpl const* function = sig->function;
        sig->type_params = ty_type_params_of(me, &function->generic);
        if (!sig->type_params.len && function->is_expression && ty_function_is_context_sensitive(function)) {
            // A function expression typed by a generic contextual signature is generic in its type parameters, which
            // its parameters were typed with (tsc's assignContextualParameterTypes): `x => x` for `<S>(x: S) => T`
            u64 key[2] = { KEY_CONTEXT_DONE, (u64)(uintptr_t)function };
            Signature const* contextual = intern_get(&my links, key, 2);
            if (contextual) sig->type_params = contextual->type_params;
        }
        LowerCtx ctx = sig->ctx;
        // A function can be anywhere, a function type is lowered knowing whether some type parameter is in scope
        ctx.generic |= !function->is_type || sig->type_params.len > 0;
        ctx.method = false; // Parameter types aren't methods (callbacks are compared with tsc's callback rule)
        u32 count = (u32)vec_function_arg_size(&function->args);
        SigParam* params = arena_new_n(&my arena, SigParam, count ? count : 1);
        bool counting_required = true;
        for_each (i, vec_function_arg, &function->args) {
            SigParam* param = &params[i_index];
            param->name = checker_atom(me, i->initializer.binding.content_span.sv);
            param->rest = i->rest;
            param->decl = i;
            param->optional = i->explicit_optional || i->initializer.value.tag != EXPR_IMPLICIT_ANY;
            param->type = ty_binding_type(me, &i->initializer.binding, ctx);
            if (param->optional || param->rest) counting_required = false;
            // (tsc's isOptionalParameter: an immediately invoked function's parameter past the arguments)
            if (function->is_iife && !i->initializer.binding.has_explicit_type && i_index >= function->iife_arg_count) {
                counting_required = false;
                if (!i->rest) param->optional = true;
            }
            if (counting_required) sig->min_args++;
        }
        if (function_is_untyped_js(function)) sig->min_args = 0;
        if (count && params[0].name == checker_atom(me, c_sv("this"))) {
            // `this: T` is about how the function is called, not an argument
            if (sig->min_args && !params[0].optional) sig->min_args--;
            sig->this_type = params[0].type;
            params++;
            count--;
        }
        sig->params = params;
        sig->param_count = count;
    }
    if (sig->class_instance) {
        sig->type_params = sig->class_type_params;
        sig->ret = sig->class_instance;
        sig->ret_state = LAZY_STATE_DONE;
    }
    if (!sig->class_instance && sig->target && sig->target->class_instance && sig->mapper) {
        // An instantiated construct signature of a class (not one a derived class without a constructor inherits: that
        // makes the derived class)
        sig->ret = ty_instantiate(me, sig->target->class_instance, sig->mapper);
        sig->ret_state = LAZY_STATE_DONE;
    }
    sig->state = LAZY_STATE_DONE;
}

Ty const* signature_return_type(Checker* me, Signature const* const_sig) {
    Signature* sig = (Signature*)const_sig;
    signature_resolve(me, sig);
    switch (sig->ret_state) {
        case LAZY_STATE_DONE: return sig->ret;
        case LAZY_STATE_IN_FLIGHT:
            // An inferred return type in progress: asking it says so, it becomes any (see ty_function_return_type())
            if (!sig->target && sig->function && !sig->function->is_type) ty_function_return_type(me, sig->function);
            return ty_intrinsic(me, TY_CIRCULAR);
        case LAZY_STATE_NOT_STARTED: break;
    }
    sig->ret_state = LAZY_STATE_IN_FLIGHT;
    Ty const* ret;
    if (sig->target) {
        ret = ty_instantiate(me, signature_return_type(me, sig->target), sig->mapper);
    } else if (sig->function && sig->function->is_type) {
        LowerCtx ctx = sig->ctx;
        ctx.generic |= sig->type_params.len > 0;
        ctx.method = false;
        ret = ty_from_node(me, &sig->function->ret, ctx);
    } else if (sig->function) {
        ret = ty_function_return_type(me, sig->function);
    } else {
        ret = ty_intrinsic(me, TY_ANY);
    }
    sig->ret = ret;
    sig->ret_state = LAZY_STATE_DONE;
    return ret;
}

bool ty_signature_return_in_flight(Checker* me, Signature const* sig) {
    for (u32 depth = 0; sig && depth < 64; depth++) {
        if (sig->ret_state == LAZY_STATE_DONE) return false;
        if (sig->ret_state == LAZY_STATE_IN_FLIGHT) return true;
        if (!sig->target) return sig->function && !sig->function->is_type && ty_function_return_in_flight(me, sig->function);
        sig = sig->target;
    }
    return false;
}

// Only ever from an annotation, so asking doesn't infer a return type (TODO: tsc 5.5 infers `x is T` for functions
// that return a narrowing expression)
TyPredicate const* signature_predicate(Checker* me, Signature const* const_sig) {
    Signature* sig = (Signature*)const_sig;
    if (sig->predicate_done) return sig->predicate;
    signature_resolve(me, sig);
    sig->predicate_done = true;
    if (sig->class_instance) return nullptr;
    if (sig->target) {
        TyPredicate const* target_predicate = signature_predicate(me, sig->target);
        if (target_predicate && target_predicate->type && sig->mapper) {
            TyPredicate* predicate = arena_new(&my arena, TyPredicate);
            *predicate = *target_predicate;
            predicate->type = ty_instantiate(me, target_predicate->type, sig->mapper);
            sig->predicate = predicate;
        } else {
            sig->predicate = target_predicate;
        }
    } else if (sig->function && sig->function->predicate) {
        TypePredicateNode const* node = sig->function->predicate;
        TyPredicate* predicate = arena_new(&my arena, TyPredicate);
        *predicate = (TyPredicate){
            .is_this = node->is_this,
            .asserts = node->asserts,
            .param_index = node->param_index,
            .param_name = checker_atom(me, node->name.sv),
        };
        // (`this: T` isn't among the parameters of the signature)
        u32 arg_count = (u32)vec_function_arg_size(&sig->function->args);
        if (predicate->param_index >= 0 && sig->param_count < arg_count) predicate->param_index--;
        if (node->has_type) {
            LowerCtx ctx = sig->ctx;
            ctx.generic |= !sig->function->is_type || sig->type_params.len > 0;
            ctx.method = false;
            predicate->type = ty_from_node(me, &node->type, ctx);
        }
        sig->predicate = predicate;
    } else if (sig->function && !sig->function->ret_explicit && sig->param_count) {
        sig->predicate = ty_infer_type_predicate(me, sig->function);
    }
    return sig->predicate;
}

// Declared to return `never` (an inferred never doesn't count for control flow)
bool signature_declared_never(Signature const* sig) {
    while (sig->target) sig = sig->target;
    return sig->function && sig->function->ret_explicit && !sig->function->predicate && sig->function->ret.tag == TYPE_NEVER;
}

typedef struct WordBuf {
    u64* data;
    u32 len, cap;
} WordBuf;

static void words_push(WordBuf* me, u64 word) {
    if (my len == my cap) {
        my cap = my cap ? my cap * 2 : 32;
        my data = realloc(my data, sizeof(u64) * my cap);
        panic_if(!my data, "out of memory");
    }
    my data[my len++] = word;
}

// The type of an object literal expression: properties with known types, interned by shape (names, types, and where
// each property was written, since the symbols baseline tells two literals of one shape apart)
Ty const* ty_object_literal(
    Checker* me, ObjectLiteralProp const* props, u32 count, IndexInfo const* index, u32 index_count, ObjectLiteralKind kind
) {
    WordBuf words = {0};
    words_push(&words, KEY_STRUCTURAL);
    words_push(&words, kind);
    for (u32 i = 0; i < count; i++) {
        if (props[i].reuse) {
            words_push(&words, 2);
            words_push(&words, (u64)(uintptr_t)props[i].reuse);
            continue;
        }
        words_push(&words, 1);
        words_push(&words, (u64)(uintptr_t)props[i].name);
        words_push(&words, props[i].flags | (u64)props[i].own << 32);
        words_push(&words, (u64)(uintptr_t)props[i].name_span.sv.buf);
        words_push(&words, props[i].type ? props[i].type->id : (u64)(uintptr_t)props[i].accessor);
    }
    for (u32 i = 0; i < index_count; i++) {
        words_push(&words, 3);
        words_push(&words, index[i].key->id);
        words_push(&words, index[i].value->id);
        words_push(&words, (u64)(uintptr_t)index[i].param_name << 1 | index[i].readonly);
    }
    Ty const* ret = intern_get(&my links, words.data, words.len);
    bool js = kind & OBJECT_JS;
    kind &= ~OBJECT_JS;
    if (!ret) {
        MembersBuilder builder = {0};
        u32 flags = TYF_OBJECT | (kind == OBJECT_LITERAL_FRESH || kind == OBJECT_JSX_ATTRIBUTES_FRESH ? TYF_FRESH_OBJECT : 0);
        for (u32 i = 0; i < count; i++) {
            if (props[i].reuse) {
                flags |= prop_type(me, props[i].reuse)->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES;
                builder_push(&builder, props, (Prop*)props[i].reuse);
                continue;
            }
            Prop* prop = arena_new(&my arena, Prop);
            prop->name = props[i].name;
            prop->flags = props[i].flags;
            prop->type = props[i].type;
            prop->type_state = props[i].type ? LAZY_STATE_DONE : LAZY_STATE_NOT_STARTED;
            prop->function = props[i].accessor;
            prop->name_span = props[i].name_span;
            prop->name_is_value = props[i].name_is_value;
            prop->literal_own = props[i].own;
            prop->numeric_redeclared = props[i].numeric_redeclared;
            prop->asserted = props[i].asserted;
            prop_add_decl_spans(me, prop, props[i].decl_spans, props[i].decl_span_count);
            // (What an accessor's type mentions isn't known yet: assume it could be anything in scope)
            flags |= props[i].type ? props[i].type->flags & (TYF_COULD_CONTAIN_TYPE_VARIABLES | TYF_NON_INFERRABLE) :
                TYF_COULD_CONTAIN_TYPE_VARIABLES;
            builder_push(&builder, props, prop);
        }
        for (u32 i = 0; i < index_count; i++) {
            flags |= (index[i].key->flags | index[i].value->flags) & TYF_COULD_CONTAIN_TYPE_VARIABLES;
            builder_push(&builder, index, index[i]);
        }
        Ty* fresh = ty_alloc(me, TY_ANONYMOUS, flags);
        fresh->anonymous.ctx = (LowerCtx){ .generic = true };
        fresh->anonymous.free_params_unknown = true;
        fresh->anonymous.object_literal = kind != OBJECT_SYNTHETIC;
        fresh->anonymous.jsx_attributes = kind == OBJECT_JSX_ATTRIBUTES || kind == OBJECT_JSX_ATTRIBUTES_FRESH;
        fresh->anonymous.js_literal = js;
        fresh->anonymous.state = arena_new(&my arena, TyObjectState);
        fresh->anonymous.state->members = builder_finish(me, &builder);
        fresh->anonymous.state->members_state = LAZY_STATE_DONE;
        *intern_slot(me, &my links, words.data, words.len) = fresh;
        ret = fresh;
    }
    free(words.data);
    return ret;
}

// tsc's WideningContext: an object literal in a union is widened knowing its siblings, whose properties it lacks it gets
// as optional `undefined` ones (`c ? { x: 1 } : { y: "" }` is `{ x: number; y?: undefined; } | { y: string; x?:
// undefined; }`), and so do the object literals in same-named properties of the siblings
typedef struct WideningCtx {
    struct WideningCtx* parent;
    Atom const* name; // The property of the parent's siblings this is the context of
    TyBuf siblings;
    bool siblings_done;
    Prop const** props; // The properties of the siblings, the last of each name, in order of first appearance
    u32 prop_count;
    bool props_done;
} WideningCtx;

static void push_members(TyBuf* out, Ty const* ty) {
    if (ty->kind == TY_UNION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) ty_buf_push(out, ty->composite.types.data[i]);
    } else {
        ty_buf_push(out, ty);
    }
}

static bool is_object_literal_type(Ty const* ty) {
    return ty->kind == TY_ANONYMOUS && ty->anonymous.object_literal;
}

// tsc's getSiblingsOfContext
static TyBuf const* context_siblings(Checker* me, WideningCtx* ctx) {
    if (ctx->siblings_done) return &ctx->siblings;
    ctx->siblings_done = true;
    TyBuf const* parent = context_siblings(me, ctx->parent);
    for (u32 i = 0; i < parent->len; i++) {
        if (!is_object_literal_type(parent->data[i])) continue;
        Prop const* prop = ty_get_prop(me, parent->data[i], ctx->name);
        if (prop && !prop->function) push_members(&ctx->siblings, prop_type(me, prop));
    }
    return &ctx->siblings;
}

// HACK: tsc's ObjectFlags.ContainsSpread, as a property that was spread into the literal (`{ ...{} }` doesn't count)
static bool contains_spread(Checker* me, Ty const* literal) {
    Members const* members = ty_members(me, literal);
    for (u32 i = 0; i < members->prop_count; i++) {
        if (!members->props[i]->literal_own) return true;
    }
    return false;
}

bool ty_is_object_literal_without_spread(Checker* me, Ty const* ty) {
    return is_object_literal_type(ty) && !contains_spread(me, ty);
}

// tsc's getPropertiesOfContext
static void context_props(Checker* me, WideningCtx* ctx) {
    if (ctx->props_done) return;
    ctx->props_done = true;
    TyBuf const* siblings = context_siblings(me, ctx);
    for (u32 i = 0; i < siblings->len; i++) {
        if (!is_object_literal_type(siblings->data[i]) || contains_spread(me, siblings->data[i])) continue;
        Members const* members = ty_members(me, siblings->data[i]);
        for (u32 j = 0; j < members->prop_count; j++) {
            Prop const* prop = members->props[j];
            u32 k = 0;
            while (k < ctx->prop_count && ctx->props[k]->name != prop->name) k++;
            if (k == ctx->prop_count) {
                ctx->props = realloc(ctx->props, sizeof(Prop const*) * (ctx->prop_count + 1));
                panic_if(!ctx->props, "out of memory");
                ctx->prop_count++;
            }
            ctx->props[k] = prop;
        }
    }
}

static void widening_ctx_drop(WideningCtx* ctx) {
    ty_buf_drop(&ctx->siblings);
    free(ctx->props);
}

static Ty const* widen_with_context(Checker* me, Ty const* ty, WideningCtx* ctx);

// tsc's getWidenedTypeOfObjectLiteral (and getRegularTypeOfObjectLiteral: the result isn't fresh)
static Ty const* widen_object_literal(Checker* me, Ty const* ty, WideningCtx* ctx) {
    Members const* members = ty_members(me, ty);
    if (ctx) context_props(me, ctx);
    u32 capacity = members->prop_count + (ctx ? ctx->prop_count : 0);
    ObjectLiteralProp* props = calloc(capacity ? capacity : 1, sizeof(ObjectLiteralProp));
    for (u32 i = 0; i < members->prop_count; i++) {
        Prop const* prop = members->props[i];
        Ty const* type = nullptr;
        if (!prop->function && ctx) {
            WideningCtx child = { .parent = ctx, .name = prop->name };
            type = widen_with_context(me, prop_type(me, prop), &child);
            widening_ctx_drop(&child);
        } else if (!prop->function) {
            type = ty_widened_objects(me, prop_type(me, prop));
        }
        props[i] = (ObjectLiteralProp){
            .name = prop->name,
            .flags = prop->flags,
            .type = type,
            .accessor = prop->function,
            .name_span = prop->name_span,
            .decl_spans = prop->decl_spans,
            .decl_span_count = prop->decl_span_count,
            .name_is_value = prop->name_is_value,
            .asserted = prop->asserted,
            .own = prop->literal_own,
            .numeric_redeclared = prop->numeric_redeclared,
        };
    }
    u32 count = members->prop_count;
    for (u32 i = 0; ctx && i < ctx->prop_count; i++) {
        Prop const* prop = ctx->props[i];
        bool has = false;
        for (u32 j = 0; j < members->prop_count && !has; j++) has = members->props[j]->name == prop->name;
        if (has) continue;
        // (tsc's getUndefinedProperty.) Under exactOptionalPropertyTypes tsc's type is missingType rather than
        // undefined: an absent value, which the property's declared-type view (getNonMissingTypeOfSymbol, what tttt
        // stores for an optional property) drops, so `x?: never` (also how tsc prints it). Index signatures and
        // optional targets accept it; reading it still gives undefined
        bool exact = compiler_options()->exact_optional_property_types && compiler_options()->strict_null_checks;
        props[count++] = (ObjectLiteralProp){
            .name = prop->name,
            .flags = PROP_OPTIONAL,
            .type = ty_intrinsic(me, exact ? TY_NEVER : TY_UNDEFINED),
            .name_span = prop->name_span,
            .decl_spans = prop->decl_spans,
            .decl_span_count = prop->decl_span_count,
            .name_is_value = prop->name_is_value,
        };
    }
    IndexInfo index[8] = {0};
    u32 index_count = members->index_count < c_arraylen(index) ? members->index_count : c_arraylen(index);
    for (u32 i = 0; i < index_count; i++) {
        index[i] = members->index[i];
        index[i].value = ty_widened_objects(me, index[i].value);
    }
    // (JSX attributes stay what they are: hyphenated names are still never excess nor weak-type failures)
    Ty const* ret = ty_object_literal(me, props, count, index, index_count,
        (ty->anonymous.jsx_attributes ? OBJECT_JSX_ATTRIBUTES : OBJECT_LITERAL) | (ty->anonymous.js_literal ? OBJECT_JS : 0));
    free(props);
    return ret;
}

// tsc's getWidenedTypeWithContext, as far as object literals go
static Ty const* widen_with_context(Checker* me, Ty const* ty, WideningCtx* ctx) {
    if (is_object_literal_type(ty)) return widen_object_literal(me, ty, ctx);
    if (ty->kind == TY_UNION && !(ty->flags & TYF_BOOLEAN)) {
        TyList types = ty->composite.types;
        u32 literals = 0;
        for (u32 i = 0; i < types.len; i++) literals += is_object_literal_type(types.data[i]);
        WideningCtx own = {0};
        if (!ctx && literals > 1) {
            push_members(&own.siblings, ty);
            own.siblings_done = true;
            ctx = &own;
        }
        TyBuf parts = {0};
        bool changed = false;
        for (u32 i = 0; i < types.len; i++) {
            Ty const* member = types.data[i];
            Ty const* widened = member->flags & TYF_NULLABLE ? member : ctx ? widen_with_context(me, member, ctx) :
                ty_widened_objects(me, member);
            changed |= widened != member;
            ty_buf_push(&parts, widened);
        }
        Ty const* ret = changed ? ty_union(me, parts.data, parts.len, ty->alias, ty->alias_args) : ty;
        ty_buf_drop(&parts);
        widening_ctx_drop(&own);
        return ret;
    }
    return ty_widened_objects(me, ty);
}

// tsc's getRegularTypeOfObjectLiteral, and getWidenedType's walk through unions, arrays and tuples to get there
static Ty const* widen_objects(Checker* me, Ty const* ty) {
    if (ty->kind == TY_UNION) return widen_with_context(me, ty, nullptr);
    if (ty->kind == TY_REFERENCE) {
        TyList args = ty_reference_args(me, ty);
        TyBuf parts = {0};
        for (u32 i = 0; i < args.len; i++) ty_buf_push(&parts, ty_widened_objects(me, args.data[i]));
        Ty const* ret = ty_reference_with_this(me, ty->reference.target, (TyList){ .data = parts.data, .len = parts.len },
            ty->reference.this_arg);
        ty_buf_drop(&parts);
        return ret;
    }
    check(ty_is_fresh_object(ty));
    return widen_object_literal(me, ty, nullptr);
}

static Ty const* widen_nullable(Checker* me, Ty const* ty, u32 depth) {
    if (depth > 16) return ty;
    if (ty->kind != TY_UNION && (ty->flags & TYF_NULLABLE)) return ty_intrinsic(me, TY_ANY);
    u64 key[2] = { KEY_WIDENED_NULLABLE, (u64)(uintptr_t)ty };
    void const* known = intern_get(&my links, key, 2);
    if (known) return known;
    Ty const* ret = ty;
    if (ty->kind == TY_UNION && !(ty->flags & TYF_BOOLEAN)) {
        // Of nothing but null and undefined: any. Else the others widen (null and undefined vanish from the union)
        TyList types = ty->composite.types;
        bool all_nullable = true, changed = false;
        TyBuf parts = {0};
        for (u32 i = 0; i < types.len; i++) {
            Ty const* member = types.data[i];
            all_nullable &= (member->flags & TYF_NULLABLE) != 0;
            Ty const* widened = member->flags & TYF_NULLABLE ? member : widen_nullable(me, member, depth + 1);
            changed |= widened != member;
            ty_buf_push(&parts, widened);
        }
        ret = all_nullable ? ty_intrinsic(me, TY_ANY) : changed ? ty_union(me, parts.data, parts.len, nullptr, (TyList){0}) : ty;
        ty_buf_drop(&parts);
    } else if (ty->kind == TY_INTERSECTION) {
        // Member by member (`{ a: undefined } & T` from a spread of a generic)
        TyList types = ty->composite.types;
        bool changed = false;
        TyBuf parts = {0};
        for (u32 i = 0; i < types.len; i++) {
            Ty const* widened = widen_nullable(me, types.data[i], depth + 1);
            changed |= widened != types.data[i];
            ty_buf_push(&parts, widened);
        }
        if (changed) ret = ty_intersection(me, parts.data, parts.len, nullptr, (TyList){0});
        ty_buf_drop(&parts);
    } else if (ty->kind == TY_REFERENCE && (ty_is_array(me, ty) || ty_is_tuple(ty))) {
        TyList args = ty_reference_args(me, ty);
        TyBuf parts = {0};
        bool changed = false;
        for (u32 i = 0; i < args.len; i++) {
            Ty const* widened = widen_nullable(me, args.data[i], depth + 1);
            changed |= widened != args.data[i];
            ty_buf_push(&parts, widened);
        }
        if (changed) {
            ret = ty_reference_with_this(me, ty->reference.target, (TyList){ .data = parts.data, .len = parts.len },
                ty->reference.this_arg);
        }
        ty_buf_drop(&parts);
    } else if (ty->kind == TY_ANONYMOUS && ty->anonymous.object_literal) {
        Members const* members = ty_members(me, ty);
        ObjectLiteralProp* props = calloc(members->prop_count ? members->prop_count : 1, sizeof(ObjectLiteralProp));
        bool changed = false;
        for (u32 i = 0; i < members->prop_count; i++) {
            Prop const* prop = members->props[i];
            Ty const* type = prop->function ? nullptr : prop_type(me, prop);
            // HACK: tsc only widens its widening null and undefined (what the `null` and `undefined` expressions are),
            // which tttt doesn't tell from written ones: a spread property (`{ ...x }` with `x: { a: undefined }`) keeps
            // its type
            Ty const* widened = type && prop->literal_own ? widen_nullable(me, type, depth + 1) : type;
            changed |= widened != type;
            props[i] = (ObjectLiteralProp){
                .name = prop->name,
                .flags = prop->flags,
                .type = widened,
                .accessor = prop->function,
                .name_span = prop->name_span,
                .decl_spans = prop->decl_spans,
                .decl_span_count = prop->decl_span_count,
                .name_is_value = prop->name_is_value,
                .asserted = prop->asserted,
                .own = prop->literal_own,
                .numeric_redeclared = prop->numeric_redeclared,
            };
        }
        IndexInfo index[8] = {0};
        u32 index_count = members->index_count < c_arraylen(index) ? members->index_count : c_arraylen(index);
        for (u32 i = 0; i < index_count; i++) {
            index[i] = members->index[i];
            index[i].value = widen_nullable(me, index[i].value, depth + 1);
            changed |= index[i].value != members->index[i].value;
        }
        if (changed) {
            ret = ty_object_literal(me, props, members->prop_count, index, index_count,
                (ty->anonymous.jsx_attributes ? (ty->flags & TYF_FRESH_OBJECT ? OBJECT_JSX_ATTRIBUTES_FRESH : OBJECT_JSX_ATTRIBUTES) :
                ty->flags & TYF_FRESH_OBJECT ? OBJECT_LITERAL_FRESH : OBJECT_LITERAL) | (ty->anonymous.js_literal ? OBJECT_JS : 0));
        }
        free(props);
    }
    *intern_slot(me, &my links, key, 2) = ret;
    return ret;
}

Ty const* ty_widened_nullable(Checker* me, Ty const* ty) {
    if (compiler_options()->strict_null_checks) return ty;
    return widen_nullable(me, ty, 0);
}

Ty const* ty_widened_objects(Checker* me, Ty const* ty) {
    if (!(ty->flags & TYF_FRESH_OBJECT)) return ty;
    u64 key[2] = { KEY_WIDENED_OBJECTS, (u64)(uintptr_t)ty };
    void const* known = intern_get(&my links, key, 2);
    if (known) return known;
    Ty const* ret = widen_objects(me, ty);
    *intern_slot(me, &my links, key, 2) = ret;
    return ret;
}

Ty const* ty_signature_type(Checker* me, Signature const* sig) {
    u64 key[2] = { KEY_SIGNATURE_TYPE, (u64)(uintptr_t)sig };
    void const** slot = intern_slot(me, &my links, key, 2);
    if (*slot) return *slot;
    MembersBuilder builder = {0};
    if (sig->flags & SIGNATURE_CONSTRUCT) {
        builder_push(&builder, construct, (Signature*)sig);
    } else {
        builder_push(&builder, call, (Signature*)sig);
    }
    Ty* ret = ty_alloc(me, TY_ANONYMOUS, TYF_OBJECT | TYF_COULD_CONTAIN_TYPE_VARIABLES);
    ret->anonymous.ctx = (LowerCtx){ .generic = true };
    ret->anonymous.free_params_unknown = true;
    ret->anonymous.state = arena_new(&my arena, TyObjectState);
    ret->anonymous.state->members = builder_finish(me, &builder);
    ret->anonymous.state->members_state = LAZY_STATE_DONE;
    *slot = ret;
    return ret;
}

Ty const* ty_param_constraint(Checker* me, Ty const* param) {
    check(param->kind == TY_TYPE_PARAM);
    TyParamState* state = param->param.state;
    if (state->constraint_state == LAZY_STATE_NOT_STARTED && state->clone_of) {
        state->constraint_state = LAZY_STATE_IN_FLIGHT;
        Ty const* constraint = ty_param_constraint(me, state->clone_of);
        state->constraint = constraint ? ty_instantiate(me, constraint, state->clone_mapper) : nullptr;
        state->constraint_state = LAZY_STATE_DONE;
    }
    if (state->constraint_state == LAZY_STATE_NOT_STARTED) {
        state->constraint_state = LAZY_STATE_IN_FLIGHT;
        TypeNode const* node = state->constraint_node;
        if (state->item) {
            node = state->item->extends_explicit ? &state->item->extends : nullptr;
        }
        state->constraint = node ? ty_from_node(me, node, (LowerCtx){ .generic = true }) : nullptr;
        if (!node && state->is_infer) state->constraint = ty_implied_infer_constraint(me, param);
        state->constraint_state = LAZY_STATE_DONE;
    }
    // In flight means the constraint is circular (`T extends T`), tsc reports TS2313 and carries on without one
    return state->constraint_state == LAZY_STATE_DONE ? state->constraint : nullptr;
}

Ty const* ty_param_default(Checker* me, Ty const* param) {
    check(param->kind == TY_TYPE_PARAM);
    TyParamState* state = param->param.state;
    if (state->fallback_state == LAZY_STATE_NOT_STARTED && state->clone_of) {
        state->fallback_state = LAZY_STATE_IN_FLIGHT;
        Ty const* fallback = ty_param_default(me, state->clone_of);
        state->fallback = fallback ? ty_instantiate(me, fallback, state->clone_mapper) : nullptr;
        state->fallback_state = LAZY_STATE_DONE;
    }
    if (state->fallback_state == LAZY_STATE_NOT_STARTED) {
        state->fallback_state = LAZY_STATE_IN_FLIGHT;
        if (state->item && state->item->fallback_explicit) {
            state->fallback = ty_from_node(me, &state->item->fallback, (LowerCtx){ .generic = true });
        }
        state->fallback_state = LAZY_STATE_DONE;
    }
    return state->fallback_state == LAZY_STATE_DONE ? state->fallback : nullptr;
}

static Ty const* compute_mapped_template(Checker* me, MappedInfo const* mapped) {
    return mapped->template_node ? ty_from_node(me, mapped->template_node, mapped->template_ctx) :
        ty_instantiate(me, ty_mapped_template(me, mapped->template_from), mapped->template_mapper);
}

Ty const* ty_mapped_template(Checker* me, MappedInfo const* mapped) {
    MappedInfo* state = (MappedInfo*)mapped; // The lazy part is private to this function
    if (state->template_state == LAZY_STATE_DONE) return state->template_;
    // Re-entered while in flight: compute again without remembering (tsc has no guard here either, the instantiation
    // limits end a runaway; an error type would lose the recursive types that do terminate)
    if (state->template_state == LAZY_STATE_IN_FLIGHT) return compute_mapped_template(me, mapped);
    state->template_state = LAZY_STATE_IN_FLIGHT;
    Ty const* template = compute_mapped_template(me, mapped);
    state->template_ = template;
    state->template_state = LAZY_STATE_DONE;
    return template;
}
