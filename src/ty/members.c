#include "../source_file.h"
#include "ty/ty_internal.h"
#include "scope.h"
#include "name_index.h"
#include "class_decl.h"
#include "enum_decl.h"
#include "compiler_options.h"

// tsc's isReadonlyAssignmentDeclaration of `Object.defineProperty(o, "x", descriptor)`: written as an object literal
// that is syntax (expr_define_property_readonly()); any other descriptor goes by its type (checkExpressionCached):
// without `value` readonly unless it has `set`, with one unless `writable` is there and not `false`
static bool define_property_readonly(Checker* me, Expr const* call) {
    Expr const* descriptor = vec_expr_at(&call->call->args, 2);
    if (descriptor->tag == EXPR_OBJECT) return expr_define_property_readonly(call);
    Ty const* type = ty_apparent_type(me, ty_expr_type(me, descriptor));
    if (!(type->flags & TYF_OBJECT)) return false;
    if (!ty_get_prop(me, type, checker_atom(me, c_sv("value")))) return !ty_get_prop(me, type, checker_atom(me, c_sv("set")));
    Prop const* writable = ty_get_prop(me, type, checker_atom(me, c_sv("writable")));
    if (!writable) return true;
    Ty const* flag = prop_type(me, writable);
    return (flag->flags & TYF_BOOLEAN_LITERAL) && !flag->literal.value.boolean;
}


// Members of object types are resolved on first use, never at construction. That's what makes recursive types legal:
// `interface List<T> { tail: List<T> }` only needs the *identity* of `List<T>` to describe its members.


// Nothing but trivia (and the first's `;`) between two class items (tsc's getSignaturesOfSymbol: `decl.pos ===
// previous.end`; `m(), m() {}` is two signatures)
static bool adjacent_methods(ClassItem const* before, ClassItem const* after) {
    Span first = before->span;
    Span second = after->span;
    if (!span_valid(first) || !span_valid(second) || span_source(first) != span_source(second)) return true;
    char const* from = span_end_ptr(first);
    char const* to = span_start_ptr(second);
    // (Decorators come first)
    if (vec_expr_size(&after->decorators)) {
        Span decorator = vec_expr_at(&after->decorators, 0)->inner_span;
        if (span_valid(decorator) && span_start_ptr(decorator) < to) to = span_start_ptr(decorator);
        while (to > from && to[-1] == '@') to--;
    }
    return from > to || span_only_trivia_between(from, to, true);
}

static const Members empty_members = {0};

PropDecl const ty_prop_decl_none = {0};
PropDecl const ty_prop_decl_value_name = { .name_is_value = true };

// What an instantiation or a synthetic property says about its declaration: nothing (read-only: writing to it faults)
static PropDecl* prop_decl_none(void) {
    return (PropDecl*)&ty_prop_decl_none;
}

// Whether decl is one of the shared read-only declarations (to write, a prop needs one of its own)
static bool prop_decl_shared(PropDecl const* decl) {
    return decl == &ty_prop_decl_none || decl == &ty_prop_decl_value_name;
}

// A property without a declaration of its own
static Prop* prop_new(Checker* me) {
    Prop* prop = arena_new(&my arena, Prop);
    prop->decl = prop_decl_none();
    return prop;
}

// A property that says where and how it was declared
static Prop* prop_new_declared(Checker* me) {
    struct { Prop prop; PropDecl decl; }* both = arena_alloc(&my arena, sizeof(*both));
    both->prop.decl = &both->decl;
    return &both->prop;
}

// The rare part of a prop's declaration, to write
static PropDeclRare* prop_decl_rare_mut(Checker* me, PropDecl* decl) {
    if (!decl->rare) decl->rare = arena_new(&my arena, PropDeclRare);
    return decl->rare;
}

// `*to = *from`, with a declaration of its own to add to
static void prop_copy_into(Checker* me, Prop* to, Prop const* from) {
    *to = *from;
    if (prop_decl_shared(from->decl)) return;
    to->decl = arena_new(&my arena, PropDecl);
    *to->decl = *from->decl;
    if (from->decl->rare) {
        to->decl->rare = arena_new(&my arena, PropDeclRare);
        *to->decl->rare = *from->decl->rare;
    }
}

static TyObjectState* object_state(Ty const* ty);

// The signatures among a class's or interface's raw members that its bases give it without a this-argument
typedef struct ThislessSignatures {
    Signature const* const* sigs;
    u32 len;
} ThislessSignatures;

typedef struct MembersBuilder {
    // builder_add_interface(): 0 every member, 1 all but the index signatures, 2 only those (see resolve_declared_members())
    uint8_t index_pass;
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
    // Members whose props have the same names in the same order as these will (an instantiation's source): their
    // props_by_name gives the order without sorting
    Members const* same_names;
    // Only the members a class names syntactically, not the computed (late-bound) ones: see early_static_members()
    bool skip_computed;
    // Only the late-bound members, after the early-bound ones of every declaration were added (tsc's
    // getResolvedMembersOrExportsOfSymbol: combineSymbolTables merges a late symbol into the early one of its name,
    // the early declarations first): see resolve_declared_members()
    bool only_late;
    // Past BUILDER_INDEXED props, where each name is among `props` (builder_find_prop(): scanning them for each member
    // added is quadratic on large interfaces), built lazily: the first `indexed` props are in it. What reorders `props`
    // calls builder_unindex() (replacing a prop by one of the same name keeps it valid)
    NameIndex by_name;
    u32 indexed;
} MembersBuilder;

enum { BUILDER_INDEXED = 16 };

#define builder_push(me, field, value) do { \
        if ((me)->field##_count == (me)->field##_cap) { \
            (me)->field##_cap = (me)->field##_cap ? (me)->field##_cap * 2 : 8; \
            (me)->field = realloc((me)->field, sizeof(*(me)->field) * (me)->field##_cap); \
            panic_if(!(me)->field, "out of memory"); \
        } \
        (me)->field[(me)->field##_count++] = (value); \
    } while (false)

static void builder_unindex(MembersBuilder* me) {
    name_index_drop(&my by_name);
    my indexed = 0;
}

// Where `name` is in the index, or the empty slot it would go to
static u64 builder_probe(MembersBuilder const* me, Atom const* name) {
    u64 slot = name_index_slot(&my by_name, (u64)(uintptr_t)name);
    while (my by_name.slots[slot] >= 0 && my props[my by_name.slots[slot]]->name != name) {
        slot = name_index_next(&my by_name, slot);
    }
    return slot;
}

static Prop* builder_find_prop(MembersBuilder* me, Atom const* name) {
    if (my props_count <= BUILDER_INDEXED) {
        for (u32 i = 0; i < my props_count; i++) {
            if (my props[i]->name == name) return my props[i];
        }
        return nullptr;
    }
    if (my by_name.slots && (u64)my props_count * 2 > my by_name.mask + 1) builder_unindex(me);
    if (!my by_name.slots) my by_name = name_index_make((isize)my props_count * 2);
    // (A name added twice stays at its first index, like the scan finds it)
    for (; my indexed < my props_count; my indexed++) {
        u64 slot = builder_probe(me, my props[my indexed]->name);
        if (my by_name.slots[slot] < 0) my by_name.slots[slot] = my indexed;
    }
    isize at = my by_name.slots[builder_probe(me, name)];
    return at >= 0 ? my props[at] : nullptr;
}

static int prop_cmp_name(void const* a, void const* b) {
    uintptr_t a_name = (uintptr_t)(*(Prop* const*)a)->name;
    uintptr_t b_name = (uintptr_t)(*(Prop* const*)b)->name;
    return a_name < b_name ? -1 : a_name > b_name;
}

// A member named by a computed name that isn't a string or numeric literal (`[Symbol.iterator]`, `[k]`): tsc's binder
// leaves it to be late bound (getDeclarationName), and late-bound members come after all the early-bound ones of all
// declarations (getResolvedMembersOrExportsOfSymbol's combineSymbolTables)
static bool name_span_is_late_bound(Span name) {
    csview sv = span_sv(name);
    if (!span_valid(name) || sv.size < 2 || sv.buf[0] != '[') return false;
    isize_t start = 1, end = sv.size - 1;
    if (sv.buf[end] != ']') return false;
    while (start < end && strchr(" \t\r\n", sv.buf[start])) start++;
    while (end > start && strchr(" \t\r\n", sv.buf[end - 1])) end--;
    if (start >= end) return false;
    char c = sv.buf[start];
    if (c == '"' || c == '\'' || c == '`') return sv.buf[end - 1] != c || end - start < 2;
    if ((c == '-' || c == '+') && end - start > 1) c = sv.buf[++start];
    if (c == '.' && end - start > 1) c = sv.buf[start + 1];
    if (c < '0' || c > '9') return true;
    for (isize_t i = start; i < end; i++) {
        char d = sv.buf[i];
        if (!((d >= '0' && d <= '9') || (d >= 'a' && d <= 'z') || (d >= 'A' && d <= 'Z') || d == '.' || d == '_')) return true;
    }
    return false;
}

// Moves the late-bound members after the others, keeping both groups' order
static void builder_late_bound_last(MembersBuilder* builder, u32 from) {
    u32 late = 0;
    for (u32 i = from; i < builder->props_count; i++) late += name_span_is_late_bound(builder->props[i]->decl->name_span);
    if (!late || late == builder->props_count - from) return;
    builder_unindex(builder);
    Prop** moved = malloc(sizeof(Prop*) * late);
    panic_if(!moved, "out of memory");
    u32 kept = from, m = 0;
    for (u32 i = from; i < builder->props_count; i++) {
        Prop* prop = builder->props[i];
        if (name_span_is_late_bound(prop->decl->name_span)) moved[m++] = prop;
        else builder->props[kept++] = prop;
    }
    memcpy(builder->props + kept, moved, sizeof(Prop*) * late);
    free(moved);
}

// tsgo's getNamedMembers (sortSymbols): properties in the order of their first declarations (file order, then
// position), those without one last by name (compareSymbols)
static bool prop_order_pos(Prop const* prop, u32* file, u32* offset) {
    while (prop && !prop->decl->decl_span_count && !span_valid(prop->decl->name_span)) prop = prop->target;
    if (!prop) return false;
    Span s = prop->decl->decl_span_count && span_source(prop->decl->decl_spans[0]) ? prop->decl->decl_spans[0] : prop->decl->name_span;
    if (!span_source(s)) return false;
    *file = span_source(s)->program_order;
    *offset = (u32)(s.buf - cstr_str(&span_source(s)->contents));
    return true;
}

typedef struct PropOrder {
    u64 pos; // file << 32 | offset, UINT64_MAX without a declaration
    Prop* prop;
    u32 index; // (Stable)
} PropOrder;

static int prop_order_cmp(PropOrder const* a, PropOrder const* b) {
    if (a->pos != b->pos) return a->pos < b->pos ? -1 : 1;
    if (a->pos == UINT64_MAX) {
        csview na = atom_sv(a->prop->name), nb = atom_sv(b->prop->name);
        // (A unique symbol's name sorts after every other: tsgo's is `\xFE@name@id`, tsc 6's `__@name@id` here)
        bool ua = na.size > 3 && na.buf[0] == '_' && memcmp(na.buf, "__@", 3) == 0;
        bool ub = nb.size > 3 && nb.buf[0] == '_' && memcmp(nb.buf, "__@", 3) == 0;
        if (ua != ub) return ua ? 1 : -1;
        isize n = na.size < nb.size ? na.size : nb.size;
        int c = n > 0 ? memcmp(na.buf, nb.buf, (size_t)n) : 0;
        if (c) return c;
        if (na.size != nb.size) return na.size < nb.size ? -1 : 1;
    }
    return a->index < b->index ? -1 : a->index > b->index;
}

// (From the `from`-th property on)
static void builder_sort_stable_from(MembersBuilder* builder, u32 from) {
    if (builder->props_count < from + 2) return;
    builder_unindex(builder);
    u32 n = builder->props_count - from;
    Prop** props = builder->props + from;
    PropOrder stack[32];
    PropOrder* order = n <= c_arraylen(stack) ? stack : malloc(sizeof(PropOrder) * n);
    panic_if(!order, "out of memory");
    bool sorted = true;
    for (u32 i = 0; i < n; i++) {
        u32 file = 0, offset = 0;
        bool has = prop_order_pos(props[i], &file, &offset);
        order[i] = (PropOrder){ has ? (u64)file << 32 | offset : UINT64_MAX, props[i], i };
        if (i && sorted) sorted = prop_order_cmp(&order[i - 1], &order[i]) < 0;
    }
    if (!sorted) {
        // (Bottom-up merge sort)
        PropOrder* tmp = malloc(sizeof(PropOrder) * n);
        panic_if(!tmp, "out of memory");
        PropOrder* src = order;
        PropOrder* dst = tmp;
        for (u32 width = 1; width < n; width *= 2) {
            for (u32 lo = 0; lo < n; lo += 2 * width) {
                u32 mid = lo + width < n ? lo + width : n, hi = lo + 2 * width < n ? lo + 2 * width : n;
                u32 a = lo, b = mid, o = lo;
                while (a < mid && b < hi) dst[o++] = prop_order_cmp(&src[b], &src[a]) < 0 ? src[b++] : src[a++];
                while (a < mid) dst[o++] = src[a++];
                while (b < hi) dst[o++] = src[b++];
            }
            PropOrder* swap = src;
            src = dst;
            dst = swap;
        }
        for (u32 i = 0; i < n; i++) props[i] = src[i].prop;
        free(tmp);
    }
    if (order != stack) free(order);
}

static void builder_sort_stable(MembersBuilder* builder) { builder_sort_stable_from(builder, 0); }

// tsc's getNamedMembers lists a class's static members declared in the class first, then the others by declaration
// (inherited ones, expandos), the synthetic `prototype`, which has no declaration, after them
static void builder_prototype_last(MembersBuilder* builder) {
    for (u32 i = 0; i < builder->props_count; i++) {
        Prop* prop = builder->props[i];
        if (!(prop->flags & PROP_PROTOTYPE)) continue;
        builder_unindex(builder);
        memmove(builder->props + i, builder->props + i + 1, sizeof(Prop*) * (builder->props_count - i - 1));
        builder->props[builder->props_count - 1] = prop;
        return;
    }
}

// The rare part of members being made, to write
static MembersRare* members_rare_mut(Checker* me, Members* members) {
    if (!members->rare) members->rare = arena_new(&my arena, MembersRare);
    return (MembersRare*)members->rare;
}

static Members const* builder_finish(Checker* me, MembersBuilder* builder) {
    builder_unindex(builder);
    Members* ret = arena_new(&my arena, Members);
    if (builder->props_count > 8) {
        Prop** sorted = arena_new_n(&my arena, Prop*, builder->props_count);
        Members const* same = builder->same_names;
        bool placed = same && same->props_by_name && same->prop_count == builder->props_count;
        if (placed) {
            // Each prop goes where its name is in the source's sorted table (unless a name is there twice)
            for (u32 i = 0; i < builder->props_count && placed; i++) {
                uintptr_t name = (uintptr_t)builder->props[i]->name;
                u32 low = 0, high = same->prop_count;
                while (low < high) {
                    u32 mid = low + (high - low) / 2;
                    if ((uintptr_t)same->props_by_name[mid]->name < name) low = mid + 1;
                    else high = mid;
                }
                placed = low < same->prop_count && same->props_by_name[low]->name == builder->props[i]->name && !sorted[low];
                if (placed) sorted[low] = builder->props[i];
            }
        }
        if (!placed) {
            memcpy(sorted, builder->props, sizeof(Prop*) * builder->props_count);
            qsort(sorted, builder->props_count, sizeof(Prop*), prop_cmp_name);
        }
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

// The members a builder holds so far, leaving it as it is
static Members const* builder_snapshot(Checker* me, MembersBuilder const* builder) {
    MembersBuilder copy = { .mapped = builder->mapped };
#define COPY(field) \
    if (builder->field##_count) { \
        copy.field = malloc(sizeof(*builder->field) * builder->field##_count); \
        panic_if(!copy.field, "out of memory"); \
        memcpy(copy.field, builder->field, sizeof(*builder->field) * builder->field##_count); \
        copy.field##_count = copy.field##_cap = builder->field##_count; \
    }
    COPY(props)
    COPY(call)
    COPY(construct)
    COPY(index)
#undef COPY
    return builder_finish(me, &copy);
}

// A class or interface whose bases are being lowered, with its own members (tsc's getBaseTypes in flight: what
// resolveObjectTypeMembers gives a type whose bases resolve circularly, see resolve_declared_members())
typedef struct BasesFrame {
    Ty const* ty;
    MembersBuilder const* builder;
    struct BasesFrame* outer;
} BasesFrame;

static Signature* signature_from_function(Checker* me, FunctionImpl const* function, LowerCtx ctx) {
    Signature* ret = arena_new(&my arena, Signature);
    ret->function = function;
    ret->ctx = ctx;
    if (function->is_constructor) ret->flags |= SIGNATURE_CONSTRUCT;
    if (function->is_abstract_constructor) ret->flags |= SIGNATURE_ABSTRACT;
    if (ctx.method) ret->flags |= SIGNATURE_METHOD;
    return ret;
}

// A function's own signature, not the JSDoc `@overload`s its type has instead (what they're checked against, TS2394)
Signature const* ty_implementation_signature(Checker* me, FunctionImpl const* function) {
    Ty const* type = ty_function_type(me, function);
    return signature_from_function(me, function, type->anonymous.ctx);
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
    if (prop_decl_shared(prop->decl)) {
        PropDecl* own = arena_new(&my arena, PropDecl);
        *own = *prop->decl;
        prop->decl = own;
    }
    // (A prop's span lists are never written once made, and are replaced to add to them)
    Span* all = arena_new_n(&my arena, Span, prop->decl->decl_span_count + count);
    if (prop->decl->decl_span_count) memcpy(all, prop->decl->decl_spans, sizeof(Span) * prop->decl->decl_span_count);
    memcpy(all + prop->decl->decl_span_count, spans, sizeof(Span) * count);
    prop->decl->decl_spans = all;
    prop->decl->decl_span_count += count;
}

// prop_add_decl_spans() of spans that live as long as the checker (the AST's, or another prop's list): a prop without
// any takes them as they are
static void prop_add_shared_decl_spans(Checker* me, Prop* prop, Span const* spans, u32 count) {
    if (count && !prop_decl_shared(prop->decl) && !prop->decl->decl_span_count) {
        prop->decl->decl_spans = spans;
        prop->decl->decl_span_count = count;
        return;
    }
    prop_add_decl_spans(me, prop, spans, count);
}

// A JavaScript method's `@overload` tags are declarations before it, at their tag names (tsgo's reparsed signatures):
// the declarations of the method's prop again, each method's overloads first
static void add_jsdoc_overload_decl_spans(Checker* me, Prop* prop, ClassDecl const* class_decl, ClassItem const* first, bool static_side) {
    u32 overloads = 0, methods = 0;
    for (ClassNamesakes it = class_namesakes(class_decl, first, false); class_namesakes_next(&it);) {
        ClassItem const* other = it.item;
        if (other->tag != CLASS_ITEM_METHOD || other->method.accessor || other->method.is_static != static_side ||
            !cstr_eq(&other->method.name_string, &first->method.name_string)
        ) {
            continue;
        }
        methods++;
        overloads += function_impl_rare(&other->method.func)->jsdoc_overload_count;
    }
    if (!overloads || prop->decl == prop_decl_none()) return;
    Span* spans = arena_new_n(&my arena, Span, overloads + methods + prop->decl->decl_span_count);
    u32 count = 0;
    for (ClassNamesakes it = class_namesakes(class_decl, first, false); class_namesakes_next(&it);) {
        ClassItem const* other = it.item;
        if (other->tag != CLASS_ITEM_METHOD || other->method.accessor || other->method.is_static != static_side ||
            !cstr_eq(&other->method.name_string, &first->method.name_string)
        ) {
            continue;
        }
        FunctionImplRare const* rare = function_impl_rare(&other->method.func);
        for (u32 o = 0; o < rare->jsdoc_overload_count; o++) {
            Span at = rare->jsdoc_overloads[o].callable->function->full_span;
            if (span_valid(at) && at.size > 1) spans[count++] = span_make_in(at, at.buf + 1, at.buf + at.size);
        }
        for (u32 k = 0; k < prop->decl->decl_span_count; k++) {
            if (span_eq(prop->decl->decl_spans[k], other->span)) spans[count++] = other->span;
        }
    }
    // (Whatever else it had, an interface's: after)
    for (u32 k = 0; k < prop->decl->decl_span_count; k++) {
        bool seen = false;
        for (u32 q = 0; q < count && !seen; q++) seen = span_eq(spans[q], prop->decl->decl_spans[k]);
        if (!seen) spans[count++] = prop->decl->decl_spans[k];
    }
    prop->decl->decl_spans = spans;
    prop->decl->decl_span_count = count;
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
    // (A type predicate is a return type node that isn't one of those: `x is this`)
    if (function->predicate) return false;
    for_each (i, vec_function_arg, &function->args) {
        InitializedBinding const* binding = &i->initializer;
        if (!binding_is_thisless(binding->binding.has_explicit_type, binding_type(&binding->binding),
                initialized_binding_value(binding)->tag != EXPR_IMPLICIT_ANY)) {
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

// The group of a symbol's declarations in one place (tsc's reorderCandidates: the merged symbol and the declaration's
// parent). Per symbol: a `declare global {}` block declares several functions, each its own group there
static TyDeclGroup const* decl_group_of(Checker* me, Symbol const* symbol, void const* node) {
    u64 key[3] = { KEY_DECL_GROUP_OF, (u64)(uintptr_t)symbol, (u64)(uintptr_t)node };
    void const** slot = intern_slot(me, &my links, key, 3);
    if (!*slot) {
        TyDeclGroup* group = arena_new(&my arena, TyDeclGroup);
        group->symbol = symbol;
        group->declaration = node;
        *slot = group;
    }
    return *slot;
}

// A member of an interface or type literal named by an entity name: late bound (tsc's hasLateBindableName)
static bool interface_member_is_late(InterfaceMember const* i) {
    return i->key.tag == INTERFACE_KEY_EXPR && i->key.expr && expr_is_entity_name(i->key.expr);
}

static void builder_add_interface_member(
    Checker* me, MembersBuilder* builder, InterfaceMember const* i, LowerCtx ctx, Symbol const* parent
) {
    if (builder->only_late && !interface_member_is_late(i)) return;
    {
        switch (i->key.tag) {
            case INTERFACE_KEY_NAME:
            case INTERFACE_KEY_EXPR: {
                Atom const* name = nullptr;
                Ty const* key = nullptr;
                if (interface_member_is_private_name(i)) break;
                if (i->key.tag == INTERFACE_KEY_NAME && span_is_bigint_name(i->key.span, i->key.name_is_value)) break;
                if (i->key.tag == INTERFACE_KEY_NAME) {
                    name = checker_atom(me, cstr_sv(&i->key.name_string));
                } else {
                    // (Late bound: not among the early members, see early_declared_members())
                    if (builder->skip_computed && interface_member_is_late(i)) break;
                    key = ty_expr_type(me, i->key.expr);
                    name = ty_literal_name(me, key);
                    if (!name) break;
                }
                bool is_method = (i->is_method || (i->key.tag == INTERFACE_KEY_NAME && i->key.name_is_member_function)) &&
                    i->type.tag == TYPE_CALLABLE && !i->accessor_kind;
                if (is_method && builder->decl_group) set_decl_group(me, i->type.callable->function, builder->decl_group);
                // (A declaration its symbol excludes is a symbol of its own, not a member: tsc's declareSymbol, TS2300)
                if (i->conflicting) break;
                u32 kind = i->accessor_kind == 1 ? 4u : i->accessor_kind == 2 ? 8u : is_method ? 2u : 1u;
                Prop* existing = builder_find_prop(builder, name);
                u32 kinds = existing ? existing->decl->member_kinds : 0;
                if (existing && i->accessor_kind && (kinds & 2u)) break; // (After a method: it conflicts, TS2300)
                if (existing && i->accessor_kind && (kinds ? !(kinds & 1u) : i->key.tag == INTERFACE_KEY_EXPR)) {
                    // The other accessor of the symbol: the getter's type is read, the setter's written (tsc's
                    // getTypeOfAccessors, getWriteTypeOfSymbol). One of a kind already there conflicts (TS2300)
                    if (kinds & kind) break;
                    // (Read: the getter's annotation, else the setter's, else the getter's body; tsc's
                    // getTypeOfAccessors. Written: the setter's annotation, else what is read)
                    bool typed = i->type.tag != TYPE_ANY || i->type.parsed;
                    if (i->accessor_kind == 2) {
                        TypeNode const* getter_type = existing->decl->node;
                        bool getter_typed = getter_type && (getter_type->tag != TYPE_ANY || getter_type->parsed);
                        if (typed && !getter_typed) {
                            existing->decl->node = &i->type;
                            if (prop_decl_rare(existing->decl)->getter_body) prop_decl_rare_mut(me, existing->decl)->getter_body = nullptr;
                        } else if (typed) {
                            prop_decl_rare_mut(me, existing->decl)->write_node = &i->type;
                        }
                        if (i->accessor) prop_decl_rare_mut(me, existing->decl)->setter_param = i->accessor->setter_param;
                    } else {
                        TypeNode const* setter_type = existing->decl->node;
                        bool setter_typed = setter_type && (setter_type->tag != TYPE_ANY || setter_type->parsed);
                        if (typed) {
                            if (setter_typed) prop_decl_rare_mut(me, existing->decl)->write_node = setter_type;
                            existing->decl->node = &i->type;
                        } else if (!setter_typed) {
                            existing->decl->node = &i->type;
                            if (i->accessor && i->accessor->getter_body && i->accessor->getter_body_infers) {
                                prop_decl_rare_mut(me, existing->decl)->getter_body = i->accessor->getter_body->function;
                            }
                        }
                    }
                    existing->flags &= ~(u32)PROP_READONLY;
                    existing->decl->member_kinds |= (uint8_t)kind;
                    { u32 n = span_valid(i->declaration); prop_add_shared_decl_spans(me, existing, &i->declaration, n); }
                    break;
                }
                if (existing && i->accessor_kind) {
                    // An accessor after a property of its name joins its symbol: a declaration more (tsgo 7.1, TypeScript
                    // issue 3613)
                    existing->decl->member_kinds |= (uint8_t)kind;
                    { u32 n = span_valid(i->declaration); prop_add_shared_decl_spans(me, existing, &i->declaration, n); }
                    break;
                }
                if (existing) {
                    // Overloads are separate members in the legacy interface and one method with several signatures here
                    // (A class's method too, which an interface of the same name adds to)
                    bool existing_method = existing->decl->node ? existing->decl->node->tag == TYPE_CALLABLE :
                        existing->decl->function != nullptr;
                    if (is_method && (existing->flags & PROP_METHOD) && existing_method) {
                        u32 count = prop_decl_rare(existing->decl)->overload_count ? prop_decl_rare(existing->decl)->overload_count : 1;
                        FunctionImpl const** functions = arena_new_n(&my arena, FunctionImpl const*, count + 1);
                        if (prop_decl_rare(existing->decl)->overload_count) {
                            memcpy(functions, prop_decl_rare(existing->decl)->overloads, sizeof(FunctionImpl const*) * count);
                        } else {
                            functions[0] = existing->decl->node ? existing->decl->node->callable->function : existing->decl->function;
                        }
                        functions[count] = i->type.callable->function;
                        prop_decl_rare_mut(me, existing->decl)->overloads = functions;
                        prop_decl_rare_mut(me, existing->decl)->overload_count = count + 1;
                        existing->thisless &= function_is_thisless(i->type.callable->function);
                        // (Optional if any overload is: tsc's binder adds SymbolFlags.Optional per declaration)
                        if (i->optional) existing->flags |= PROP_OPTIONAL;
                        { u32 n = span_valid(i->declaration); prop_add_shared_decl_spans(me, existing, &i->declaration, n); }
                    } else if (!is_method && !(existing->flags & PROP_METHOD) && existing->decl->node && !i->accessor_kind) {
                        // A property declared again (TS2717): one symbol, the first declaration's type. String named only
                        // if every declaration is (tsc asks each one)
                        { u32 n = span_valid(i->declaration); prop_add_shared_decl_spans(me, existing, &i->declaration, n); }
                        if (i->key.tag == INTERFACE_KEY_NAME && !existing->decl->name_form && span_valid(existing->decl->name_span)) {
                            existing->decl->name_form = ty_prop_name_form_merge(
                                ty_prop_name_form(span_sv(existing->decl->name_span), nullptr, nullptr, false),
                                ty_prop_name_form(span_sv(i->key.span), nullptr, nullptr, false));
                        }
                    } else if (!is_method && !i->accessor_kind && !(existing->flags & PROP_METHOD)) {
                        // A property after a class accessor of its name (a merged class) joins its symbol, which stays
                        // what it was: only a declaration more. (After a method it conflicts: a symbol of its own, TS2300)
                        { u32 n = span_valid(i->declaration); prop_add_shared_decl_spans(me, existing, &i->declaration, n); }
                    } else {
                        break;
                    }
                    existing->decl->member_kinds |= (uint8_t)kind;
                    break;
                }
                Prop* prop = prop_new_declared(me);
                prop->name = name;
                prop->parent = i->origin_interface ? i->origin_interface : parent;
                prop->decl->node = &i->type;
                if (i->accessor && i->accessor->getter_body && i->accessor->getter_body_infers) {
                    prop_decl_rare_mut(me, prop->decl)->getter_body = i->accessor->getter_body->function;
                }
                if (i->accessor_kind == 2 && i->accessor) prop_decl_rare_mut(me, prop->decl)->setter_param = i->accessor->setter_param;
                prop->decl->member_kinds = (uint8_t)kind;
                prop->decl->ctx = ctx;
                prop->decl->ctx.method = is_method;
                prop->thisless = i->type.tag == TYPE_CALLABLE ?
                    function_is_thisless(i->type.callable->function) : type_node_is_thisless(&i->type);
                if (i->optional) prop->flags |= PROP_OPTIONAL;
                if (i->readonly || i->accessor_kind == 1) prop->flags |= PROP_READONLY; // (A getter without a setter)
                if (is_method) prop->flags |= PROP_METHOD;
                prop->decl->name_span = i->key.span;
                prop->decl->name_is_value = i->key.tag == INTERFACE_KEY_NAME && i->key.name_is_value;
                if (key) prop->decl->name_form = ty_prop_name_form(span_sv(i->key.span), i->key.expr, key, false);
                // (A computed number name is a number to keyof: `{ [0]: X }` has keyof 0, tsc's getLiteralTypeFromProperty)
                // (A computed enum member name is that member, tsc's late bound nameType: keyof gives it, it prints `[E.A]`)
                if (key && (key->flags & (TYF_NUMBER_LITERAL | TYF_ENUM_LITERAL))) prop_decl_rare_mut(me, prop->decl)->name_type = ty_regular(me, key);
                { u32 n = span_valid(i->declaration); prop_add_shared_decl_spans(me, prop, &i->declaration, n); }
                builder_push(builder, props, prop);
            }   break;

            case INTERFACE_KEY_INDEX: {
                // (tsc's getIndexInfosOfIndexSymbol: only a signature with exactly one typed parameter)
                if (!i->key.index->valid) break;
                // TODO: index signature value types are lowered eagerly, unlike property types
                IndexInfo info = {
                    .key = ty_from_node(me, &i->key.index->type, ctx),
                    .value = ty_from_node(me, &i->type, ctx),
                    .param_name = checker_atom(me, span_sv(i->key.index->name)),
                    .readonly = i->readonly,
                    .decl_span = i->declaration,
                };
                // `[k: A | B]` is a signature for each valid key type of the union, the first one for a key type counts
                // (tsc's getIndexInfosOfIndexSymbol)
                Ty const* key = info.key;
                bool single = key->kind != TY_UNION || (key->flags & TYF_BOOLEAN);
                for (u32 k = 0; k < (single ? 1 : key->composite.types.len); k++) {
                    Ty const* member = single ? key : key->composite.types.data[k];
                    if (!ty_is_valid_index_key(member)) continue;
                    bool seen = false;
                    for (u32 j = 0; j < builder->index_count && !seen; j++) seen = builder->index[j].key == member;
                    if (seen) continue;
                    info.key = member;
                    builder_push(builder, index, info);
                }
            }   break;

            case INTERFACE_KEY_MAPPED: {
                LowerCtx mapped_ctx = ctx;
                mapped_ctx.generic = true;
                MappedInfo* mapped = arena_new(&my arena, MappedInfo);
                mapped->type_param = i->key.mapped->owned_symbol ?
                    ty_type_param(me, i->key.mapped->owned_symbol) :
                    ty_synthetic_type_param(me, span_sv(i->key.mapped->type_name));
                mapped->constraint = ty_from_node(me, &i->key.mapped->in_type, mapped_ctx);
                // `[P in any]` is over tsc's keyofConstraintType (getConstraintFromTypeParameter)
                if (mapped->constraint == ty_intrinsic(me, TY_ANY)) {
                    Ty const* parts[3] = { ty_intrinsic(me, TY_STRING), ty_intrinsic(me, TY_NUMBER), ty_intrinsic(me, TY_ES_SYMBOL) };
                    mapped->constraint = ty_union(me, parts, 3, nullptr, (TyList){0});
                }
                if (i->key.mapped->has_as_type) {
                    mapped->name_type = ty_from_node(me, &i->key.mapped->as_type, mapped_ctx);
                }
                mapped->template_node = &i->type;
                mapped->template_ctx = mapped_ctx;
                TypeNode const* in_type = &i->key.mapped->in_type;
                if (in_type->tag == TYPE_KEYOF) {
                    mapped->modifiers_type = ty_from_node(me, &in_type->keyof->interface, mapped_ctx);
                    mapped->keyof_constraint = true;
                } else if (mapped->constraint->kind == TY_TYPE_PARAM) {
                    // `[P in K]` with `K extends keyof T` (Pick) takes its modifiers from T too
                    // (A distributed K's: its original's, tsgo 7.1's getModifiersTypeFromMappedType)
                    Ty const* constraint = ty_param_constraint(me, ty_non_distributed(mapped->constraint));
                    if (constraint && constraint->kind == TY_KEYOF) mapped->modifiers_type = constraint->keyof.operand;
                }
                mapped->readonly = i->key.mapped->readonly;
                mapped->optional = i->key.mapped->optional;
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
                    if (span_valid(i->declaration)) sig->decl_span = i->declaration;
                }
            }   break;
        }
    }
}

// Like computed_index_kind() for a member of an interface or type literal
static u32 interface_computed_index_kind(Checker* me, InterfaceMember const* i) {
    if (i->key.tag != INTERFACE_KEY_EXPR || !i->key.expr || !expr_is_entity_name(i->key.expr)) return 0;
    Ty const* key_type = ty_regular(me, ty_expr_type(me, i->key.expr));
    if (key_type->flags & (TYF_LITERAL | TYF_UNIQUE_ES_SYMBOL | TYF_ENUM)) return 0;
    Ty const* keys[3] = { ty_intrinsic(me, TY_STRING), ty_intrinsic(me, TY_NUMBER), ty_intrinsic(me, TY_ES_SYMBOL) };
    if (!ty_is_related(me, key_type, ty_union(me, keys, 3, nullptr, (TyList){0}), TY_RELATION_ASSIGNABLE)) return 0;
    bool number = ty_is_related(me, key_type, keys[1], TY_RELATION_ASSIGNABLE);
    bool symbol = ty_is_related(me, key_type, keys[2], TY_RELATION_ASSIGNABLE);
    u32 makes = number ? 1 : symbol ? 2 : 0;
    return (symbol ? 0 : 1) | (number ? 2 : 0) | (symbol ? 4 : 0) | 8 | (makes << 4);
}

static void builder_add_interface(
    Checker* me, MembersBuilder* builder, Interface const* interface, LowerCtx ctx, Symbol const* parent
) {
    bool computed[3] = {0}, readonly[3] = { true, true, true };
    for_each (i, vec_interface_member, &interface->members) {
        bool is_index = i->key.tag == INTERFACE_KEY_INDEX;
        if (builder->index_pass == 1 && is_index) continue;
        if (builder->index_pass != 2 || is_index) builder_add_interface_member(me, builder, i, ctx, parent);
        if (builder->index_pass == 1 || builder->skip_computed) continue;
        u32 kind = interface_computed_index_kind(me, i);
        if (!kind) continue;
        computed[kind >> 4] = true;
        readonly[kind >> 4] &= i->readonly;
    }
    if (builder->index_pass == 1 || builder->skip_computed) return; // (The computed names' index signatures with the others)
    // A member named by an expression of type string, number or symbol makes an index signature for that key of every
    // member (tsc's getIndexInfosOfIndexSymbol with hasLateBindableIndexSignature, see builder_add_computed_index_infos())
    static TyIntrinsic const keys[3] = { TY_STRING, TY_NUMBER, TY_ES_SYMBOL };
    for (u32 k = 0; k < 3; k++) {
        if (!computed[k]) continue;
        Ty const* key = ty_intrinsic(me, keys[k]);
        bool declared = false;
        for (u32 j = 0; j < builder->index_count; j++) declared |= builder->index[j].key == key;
        if (declared) continue;
        TyBuf types = {0};
        for_each (i, vec_interface_member, &interface->members) {
            if (!(interface_computed_index_kind(me, i) & (1u << k))) continue;
            // (The type of each member's symbol: an optional one's has undefined, getTypeOfSymbol)
            Ty const* type = ty_from_node(me, &i->type, ctx);
            ty_buf_push(&types, i->optional ? ty_optional_prop_type(me, type) : type);
        }
        for (u32 j = 0; j < builder->props_count; j++) {
            Atom const* name = builder->props[j]->name;
            bool symbol_name = ty_unique_symbol_of_name(me, name) != nullptr;
            bool included = k == 0 ? !symbol_name : k == 1 ? ty_is_numeric_name(name) : symbol_name;
            if (included) {
                Ty const* type = prop_type(me, builder->props[j]);
                ty_buf_push(&types, builder->props[j]->flags & PROP_OPTIONAL ? ty_optional_prop_type(me, type) : type);
            }
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

typedef struct SyntheticParts {
    u32 len;
    Prop const* props[];
} SyntheticParts;

// The properties named `name` that `ty` (through its apparent type) has, appended to `out`: an object's, or each of a
// union's members' if every member has one (tsc's getPropertiesOfType of a union). How many were appended
static u32 append_part_props(Checker* me, Ty const* ty, Atom const* name, Prop const*** out, u32* len, u32* cap) {
    if (!(ty->flags & TYF_OBJECT)) ty = ty_apparent_type(me, ty);
    TyList members = ty->kind == TY_UNION ? ty->composite.types : (TyList){ .data = &ty, .len = 1 };
    u32 before = *len;
    for (u32 i = 0; i < members.len; i++) {
        Ty const* member = members.data[i];
        if (!(member->flags & TYF_OBJECT)) member = ty_apparent_type(me, member);
        Prop const* prop = member->flags & TYF_OBJECT ? ty_get_prop(me, member, name) : nullptr;
        if (!prop) {
            *len = before;
            return 0;
        }
        if (*len == *cap) {
            *cap = *cap ? *cap * 2 : 8;
            *out = realloc(*out, sizeof(Prop const*) * *cap);
            panic_if(!*out, "out of memory");
        }
        (*out)[(*len)++] = prop;
    }
    return *len - before;
}

// Every member of every part of an intersection (only the properties if `props_only`); a property several parts have
// is one of the intersection's own, with the intersected type (tsc's getPropertiesOfType on the base,
// createUnionOrIntersectionProperty). A part whose apparent type is a union (`T[K]` constrained to one) has the
// properties every member has, a synthetic one of theirs
static void builder_add_parts_members(Checker* me, MembersBuilder* builder, Ty const* base, TyList base_parts, bool props_only) {
    Prop const** found_props = nullptr;
    u32 found_len = 0, found_cap = 0;
    for (u32 i = 0; i < base_parts.len; i++) {
        Ty const* part = base_parts.data[i];
        if (!(part->flags & TYF_OBJECT)) part = ty_apparent_type(me, part);
        bool union_part = part->kind == TY_UNION && part->composite.types.len;
        Ty const* listed = union_part ? ty_apparent_type(me, part->composite.types.data[0]) : part;
        if (!(listed->flags & TYF_OBJECT)) continue;
        Members const* members = ty_members(me, listed);
        for (u32 j = 0; j < members->prop_count; j++) {
            Prop* prop = members->props[j];
            if (builder_find_prop(builder, prop->name)) continue;
            found_len = 0;
            u32 own = 0;
            for (u32 k = 0; k < base_parts.len; k++) {
                u32 added = append_part_props(me, base_parts.data[k], prop->name, &found_props, &found_len, &found_cap);
                if (k == i) own = added;
            }
            if (!own) continue; // (Not every member of this part's union has it)
            if (found_len > 1 && ty_props_distinct(me, found_props, found_len)) {
                FoundProp found = ty_find_prop(me, base, prop->name);
                if (!found.found) continue;
                Prop* merged = arena_new(&my arena, Prop);
                *merged = (Prop){
                    .name = prop->name, .flags = found.flags, .parent = prop->parent, .target = prop,
                    .type = found.type, .type_state = LAZY_STATE_DONE, .decl = prop_decl_none(), .synthetic = true,
                };
                SyntheticParts* parts = arena_alloc(&my arena, sizeof(SyntheticParts) + sizeof(Prop const*) * found_len);
                parts->len = found_len;
                memcpy(parts->props, found_props, sizeof(Prop const*) * found_len);
                *intern_slot(me, &my links, (u64[]){ KEY_SYNTHETIC_PROP_PARTS, (u64)(uintptr_t)merged }, 2) = parts;
                prop = merged;
            }
            builder_push(builder, props, prop);
        }
        if (props_only || union_part) continue;
        for (u32 j = 0; j < members->call_count; j++) builder_push(builder, call, members->call[j]);
        for (u32 j = 0; j < members->construct_count; j++) builder_push(builder, construct, members->construct[j]);
        for (u32 j = 0; j < members->index_count; j++) {
            bool has_own = false;
            for (u32 k = 0; k < builder->index_count; k++) has_own |= builder->index[k].key == members->index[j].key;
            if (!has_own) builder_push(builder, index, members->index[j]);
        }
    }
    free(found_props);
}

bool ty_props_distinct(Checker* me, Prop const* const* props, u32 len) {
    Prop const* root = props[0];
    while (root->target) root = root->target;
    for (u32 i = 1; i < len; i++) {
        if (props[i] == props[0]) continue;
        Prop const* other = props[i];
        while (other->target) other = other->target;
        if (other != root || prop_type(me, props[i]) != prop_type(me, props[0])) return true;
    }
    return false;
}

static void builder_add_intersection_members(Checker* me, MembersBuilder* builder, Ty const* base, bool props_only) {
    builder_add_parts_members(me, builder, base, base->composite.types, props_only);
}

u32 ty_synthetic_prop_parts(Checker* me, Prop const* prop, Prop const* const** out) {
    while (prop && !prop->synthetic) prop = prop->target;
    if (!prop) return 0;
    SyntheticParts const* parts = intern_get(&my links, (u64[]){ KEY_SYNTHETIC_PROP_PARTS, (u64)(uintptr_t)prop }, 2);
    if (!parts) return 0;
    *out = parts->props;
    return parts->len;
}

static void builder_add_inherited(Checker* me, MembersBuilder* builder, Ty const* base) {
    // (A mixin's base can be a type parameter or `object`: what it has is its apparent type's)
    if (!(base->flags & (TYF_OBJECT | TYF_INTERSECTION))) base = ty_apparent_type(me, base);
    if (base->kind == TY_INTERSECTION) {
        builder_add_intersection_members(me, builder, base, false);
        return;
    }
    // (A base type parameter or indexed access whose constraint is a union: `extends NoopBase<T[K]>`)
    if (base->kind == TY_UNION) {
        builder_add_parts_members(me, builder, base, (TyList){ .data = &base, .len = 1 }, false);
        return;
    }
    Members const* members = ty_members(me, base);
    // A base's props have distinct names, so each is looked for among the props there were before it: by a hash of
    // their names when both are big (DOM interfaces extend chains of interfaces of hundreds of members each)
    u32 own = builder->props_count;
    u32 hash_cap = 0;
    u32* slots = nullptr; // Index + 1 into builder->props, 0 for empty
    if (own > 16 && members->prop_count > 16) {
        hash_cap = 64;
        while (hash_cap < own * 2) hash_cap *= 2;
        slots = calloc(hash_cap, sizeof(u32));
        panic_if(!slots, "out of memory");
        for (u32 j = 0; j < own; j++) {
            u32 h = (u32)(((uintptr_t)builder->props[j]->name >> 3) * 0x9E3779B1u) & (hash_cap - 1);
            while (slots[h] && builder->props[slots[h] - 1]->name != builder->props[j]->name) h = (h + 1) & (hash_cap - 1);
            if (!slots[h]) slots[h] = j + 1;
        }
    }
    for (u32 i = 0; i < members->prop_count; i++) {
        // A static `#x` stays the base's (tsc's isStaticPrivateIdentifierProperty in addInheritedMembers)
        if ((members->props[i]->flags & PROP_STATIC) && ty_private_name_class(me, members->props[i]->name)) continue;
        Atom const* name = members->props[i]->name;
        Prop* derived = nullptr;
        if (slots) {
            u32 h = (u32)(((uintptr_t)name >> 3) * 0x9E3779B1u) & (hash_cap - 1);
            while (slots[h] && builder->props[slots[h] - 1]->name != name) h = (h + 1) & (hash_cap - 1);
            if (slots[h]) derived = builder->props[slots[h] - 1];
        } else {
            for (u32 j = 0; j < own && !derived; j++) if (builder->props[j]->name == name) derived = builder->props[j];
        }
        // (A JavaScript `this.x = v` member overrides too: tsgo's addInheritedMembers)
        if (!derived) builder_push(builder, props, members->props[i]);
    }
    free(slots);
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

// A construct signature of a base constructor that takes the written type arguments (none if `written` is null): as
// many as it has type parameters, or fewer if the rest have defaults (tsc's getInstantiatedConstructorsForTypeArguments
// and getDefaultConstructSignatures). The mapper (null if it has no type parameters) fills them in. False if the
// signature doesn't take them
static bool base_signature_type_args(
    Checker* me, Signature const* base_sig, VecSpannedType const* written, LowerCtx ctx, TyMapper const** mapper
) {
    signature_resolve(me, base_sig);
    TyList params = base_sig->type_params;
    u32 count = written ? (u32)vec_spanned_type_size(written) : 0;
    *mapper = nullptr;
    if (count > params.len) return false;
    if (!params.len) return true;
    Ty const** args = arena_new_n(&my arena, Ty const*, params.len);
    // (Those not filled in yet are the error type while a default is instantiated: tsc's fillMissingTypeArguments)
    for (u32 j = 0; j < params.len; j++) args[j] = ty_intrinsic(me, TY_ERROR);
    for (u32 j = 0; j < params.len; j++) {
        if (j < count) {
            args[j] = ty_from_node(me, &vec_spanned_type_at(written, j)->type, ctx);
        } else {
            // A default can mention the parameters before it (tsc's fillMissingTypeArguments)
            Ty const* fallback = ty_param_default(me, params.data[j]);
            args[j] = fallback ? ty_instantiate(me, fallback, ty_mapper_new(me, params, (TyList){ .data = args, .len = params.len })) : nullptr;
        }
        if (!args[j]) return false;
    }
    *mapper = ty_mapper_new(me, params, (TyList){ .data = args, .len = params.len });
    return true;
}

// tsc's getConstructorsForTypeArguments is not empty: a construct signature takes `count` type arguments (at least as
// many as it requires, outside JavaScript). True without construct signatures: that is TS2507's
bool ty_base_constructors_fit(Checker* me, Ty const* constructor, u32 count, bool is_js) {
    Members const* members = ty_members(me, constructor);
    if (!members->construct_count) return true;
    for (u32 i = 0; i < members->construct_count; i++) {
        Signature const* sig = members->construct[i];
        signature_resolve(me, sig);
        TyList params = sig->type_params;
        u32 min = 0;
        for (u32 j = 0; j < params.len; j++) {
            if (!ty_param_default(me, params.data[j])) min = j + 1;
        }
        // (tsgo's getConstructorsForTypeArguments has no JavaScript leniency: a class reference has its own, see
        // ty_class_base_constructor_unfit())
        (void)is_js;
        if (count >= min && count <= params.len) return true;
    }
    return false;
}

// (SymbolLinks.base_cycle; BASE_CYCLE_CONSTRUCTOR: the class's `extends` expression needed the class, see class_base())
enum { BASE_CYCLE_KNOWN = 1, BASE_CYCLE_IN_CYCLE = 2, BASE_CYCLE_REENTERED = 4, BASE_CYCLE_CONSTRUCTOR = 8 };

// An object type, `object`, a type parameter constrained to one or an intersection of them (tsc's isValidBaseType).
// Unlike tsc, not `any`: a class extending `any` has no base as far as class_base() goes
static bool is_valid_base_type(Checker* me, Ty const* ty, u32 depth) {
    if (depth > 16) return false;
    if (depth == 0) ty = ty_reduced(me, ty); // (tsc's resolveBaseTypesOfClass: getReducedType of the base)
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

// tsc's isValidBaseType, which takes `any`: what an interface may extend and a class implement (TS2312, TS2422)
bool ty_is_valid_base_type(Checker* me, Ty const* ty) {
    return (ty->flags & TYF_ANY) || is_valid_base_type(me, ty, 0);
}

// The class a class extends: its instance type (with the written type arguments) and its declaration. False if
// there's none. HACK: a class name (`extends Name`, `extends Name<Args>`, `extends A.B.Name`) is found syntactically;
// any other expression is the return type of its first construct signature that takes the written type arguments
// (that they all construct the same type, TS2510, is ty_class_base_constructors_differ()'s)
static bool class_base(Checker* me, ClassDecl const* class_decl, LowerCtx ctx, Ty const** instance, Symbol const** base_decl) {
    Expr const* extends = &class_decl->extends;
    SourceFile const* file = span_source(extends->inner_span);
    bool js = file && file->is_js;
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
        Symbol const* namespace_decl = nullptr;
        if (object->tag == EXPR_IDENTIFIER) namespace_decl = object->identifier.target;
        namespace_decl = symbol_for_meaning(namespace_decl, SYMBOL_NAMESPACE);
        if (namespace_decl && !namespace_decl->is_placeholder) {
            decl = symbol_get_export(namespace_decl, span_sv(property_name(extends->property)));
        }
    }
    // The name was looked up as a value, the class is the same symbol
    decl = symbol_for_meaning(decl, SYMBOL_CLASS);
    if (!decl || decl->is_placeholder) {
        // Any other expression (a mixin call, a class expression, a variable of a constructor type): what its first
        // construct signature that takes the written type arguments returns. TODO: the static side, several signatures
        if (extends->tag == EXPR_IMPLICIT_ANY) return false;
        // (Under tsc's pushTypeResolution(ResolvedBaseConstructorType): an expression that needs the class's bases is
        // circular, TS2506, and the class has no base)
        SymbolLinks* links = class_decl->symbol ? ty_symbol_links(me, class_decl->symbol) : nullptr;
        if (links && (links->base_cycle & BASE_CYCLE_CONSTRUCTOR)) return false;
        if (ty_resolving_find(me, RESOLVING_BASE_CONSTRUCTOR, class_decl)) {
            ty_resolving_cycle(me, RESOLVING_BASE_CONSTRUCTOR, class_decl);
            return false;
        }
        ty_resolving_push(me, RESOLVING_BASE_CONSTRUCTOR, class_decl);
        my base_constructors_in_flight++;
        Ty const* expr_type = ty_expr_type(me, extends);
        my base_constructors_in_flight--;
        if (ty_resolving_pop(me) && links) {
            links->base_cycle |= BASE_CYCLE_CONSTRUCTOR;
            return false;
        }
        Ty const* constructor = ty_apparent_type(me, expr_type);
        if (!(constructor->flags & (TYF_OBJECT | TYF_INTERSECTION))) return false;
        Members const* members = ty_members(me, constructor);
        Ty const* constructed = nullptr;
        for (u32 i = 0; i < members->construct_count && !constructed; i++) {
            TyMapper const* mapper;
            if (!base_signature_type_args(me, members->construct[i], written_args, ctx, &mapper)) continue;
            constructed = signature_return_type(me, members->construct[i]);
            if (mapper) constructed = ty_instantiate(me, constructed, mapper);
        }
        if (!constructed || !is_valid_base_type(me, constructed, 0)) {
            if (constructed) *instance = constructed; // (For ty_class_never_base())
            return false;
        }
        *instance = constructed;
        *base_decl = nullptr;
        return true;
    }
    // (The wrong number of type arguments makes the reference errorType, which is no base: tsc's resolveBaseTypesOfClass)
    if (!class_decl_base_arity_fits(class_decl)) return false;
    Ty const* base = ty_declared_type_of(me, decl);
    if (base->kind != TY_DECLARED) return false;
    // (A base class without construct signatures, its default ones filtered by its own base's type arguments, isn't a
    // constructor type, TS2507: the base constructor is errorType, no base. Only a derived base class can lack them)
    SymbolDecl const* base_node = symbol_find_decl(decl, SYMBOL_DECL_CLASS);
    if (base_node && ((ClassDecl const*)base_node->node)->extends.tag != EXPR_IMPLICIT_ANY &&
        !ty_members(me, ty_static_type_of(me, decl))->construct_count
    ) {
        return false;
    }
    *base_decl = decl;
    TyList params = base->declared.type_params;
    if (!params.len) {
        *instance = base;
        return true;
    }
    Ty const* args[32];
    if (params.len > c_arraylen(args)) return false;
    u32 outer = base->declared.outer_count; // (A local base class: the same outer type parameters)
    for (u32 i = 0; i < params.len; i++) args[i] = ty_intrinsic(me, TY_ERROR);
    for (u32 i = 0; i < params.len; i++) {
        if (i < outer) {
            args[i] = params.data[i];
        } else if (written_args && (isize_t)(i - outer) < vec_spanned_type_size(written_args)) {
            args[i] = ty_from_node(me, &vec_spanned_type_at(written_args, i - outer)->type, ctx);
        } else {
            Ty const* fallback = ty_param_default(me, params.data[i]);
            // In JavaScript a default of `{}` or unknown is any (tsc's fillMissingTypeArguments, isJavaScriptImplicitAny)
            if (fallback && js && ((fallback->flags & TYF_UNKNOWN) ||
                ty_is_related(me, fallback, ty_empty_object_type(me), TY_RELATION_IDENTITY))
            ) {
                fallback = nullptr;
            }
            if (fallback) {
                // A default can mention the parameters before it, later ones are the error type (tsc's
                // fillMissingTypeArguments)
                fallback = ty_instantiate(me, fallback, ty_mapper_new(me, params, (TyList){ .data = args, .len = params.len }));
            }
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
        // (Not a constructor type, TS2507: the base constructor is the error type, tsc's getBaseConstructorTypeOfClass)
        Ty const* apparent = ty_apparent_type(me, type);
        bool constructor = (apparent->flags & (TYF_OBJECT | TYF_INTERSECTION)) && ty_members(me, apparent)->construct_count;
        variable = constructor ? type : nullptr;
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
// (Its own type parameters count: tsc's binder puts them in the symbol's members, so a generic class or interface is
// never its base, getSingleBaseForNonAugmentingSubtype. Outer ones don't)
static bool declaration_has_members(SymbolDecl const* node) {
    if (node->kind == SYMBOL_DECL_INTERFACE) {
        InterfaceDecl const* interface_decl = node->node;
        return vec_interface_member_size(&interface_decl->type.interface.members) != 0 ||
            vec_generic_item_size(&interface_decl->generic.items) != 0;
    }
    if (node->kind != SYMBOL_DECL_CLASS) return false;
    if (vec_generic_item_size(&((ClassDecl const*)node->node)->generic.items)) return true;
    for_each (i, vec_class_item, &((ClassDecl const*)node->node)->items) {
        if (i->tag == CLASS_ITEM_STATIC_BLOCK) continue;
        if (i->tag == CLASS_ITEM_INTERFACE_MEMBER ? !i->interface_member_static : !class_item_is_static(i)) return true;
    }
    return false;
}

static bool has_base_type(Checker* me, Ty const* source, Ty const* target);

static Ty const* non_augmenting_base_uncached(Checker* me, Ty const* ty) {
    Ty const* target = ty->kind == TY_REFERENCE ? ty->reference.target : ty;
    Symbol const* decl = target->declared.symbol;
    if (!decl || symbol_rare(decl)->members) return nullptr; // (JavaScript `this.x = v` members)
    // A class, or an interface that is a reference in tsc (generic, or mentioning `this`)
    bool is_class = target->declared.kind == TY_DECLARED_CLASS_INSTANCE;
    // (An interface extending any is a reference too, tsc's isThislessInterface: its base isn't an interface. Then it
    // is any, unless it has members)
    bool maybe_any_base = false;
    if (!is_class && !target->declared.type_params.len && !target->declared.this_param) {
        for (SymbolDecl const* node = decl->decls; node < decl->decls + decl->decl_count; node++) {
            if (declaration_has_members(node)) return nullptr;
        }
        maybe_any_base = true;
    }
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
                bool any = (type->flags & TYF_ANY) && !(type->flags & TYF_ERROR);
                if (!any && (maybe_any_base || !(type->flags & (TYF_OBJECT | TYF_INTERSECTION)))) continue;
                if (base) return nullptr;
                base = type;
            }
        }
    }
    if (maybe_any_base && !(base && (base->flags & TYF_ANY))) return nullptr;
    if (base && (base->flags & TYF_ANY)) return base;
    // (A circular base is none: tsc's resolveBaseTypesOfClass)
    if (!base || base == target || has_base_type(me, base, target)) return nullptr;
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

// Whether a class member's computed name can name it: a literal (the binder's name), or an entity name, which does when
// its type is a literal or unique symbol (tsc's isLateBindableName). Anything else is left out without being checked
static bool computed_key_may_name(Expr const* key) {
    if (expr_is_perenthisized(key)) return false;
    if (key->tag == EXPR_LITERAL) return key->literal.tag == VALUE_STRING || key->literal.tag == VALUE_NUMBER;
    if (key->tag == EXPR_UNARY_OP && (key->unary_op->op == UNARY_OP_MINUS || key->unary_op->op == UNARY_OP_PLUS)) {
        Expr const* operand = &key->unary_op->expr;
        return operand->tag == EXPR_LITERAL && operand->literal.tag == VALUE_NUMBER && !expr_is_perenthisized(operand);
    }
    return expr_is_entity_name(key);
}

// A class member named by an entity name: late bound (tsc's hasLateBindableName; a literal key is bound early, by value)
static bool class_item_is_late(ClassItem const* item) {
    Expr const* key = item->tag == CLASS_ITEM_FIELD ? item->field.computed_key : item->tag == CLASS_ITEM_METHOD ? item->method.computed_key : nullptr;
    return key && computed_key_may_name(key) && !computed_key_early_name(key, nullptr);
}

// The meanings of a member's symbol, as tsc's binder flags them: 1 property, 2 method, 4 get, 8 set
static u32 class_item_meanings(ClassItem const* item) {
    if (item->tag == CLASS_ITEM_FIELD) return item->field.is_auto_accessor ? 4 | 8 : 1;
    return item->method.accessor == CLASS_ACCESSOR_GET ? 4 : item->method.accessor == CLASS_ACCESSOR_SET ? 8 : 2;
}

static u32 prop_meanings(Prop const* prop) {
    if (prop->flags & PROP_METHOD) return 2;
    if (!(prop->flags & PROP_ACCESSOR)) return 1;
    return (prop->flags & PROP_CLASS_GETTER ? 4 : 0) | (prop->flags & PROP_READONLY ? 0 : 8);
}

// What the meanings exclude (TypeScript 7: a property excludes methods, a method everything but methods, an accessor
// everything but the other accessor and properties)
static u32 meanings_excluded(u32 meanings) {
    if (meanings & 2) return 1 | 4 | 8;
    if (meanings == 1) return 2;
    return 2 | (meanings & 4 ? 4 : 0) | (meanings & 8 ? 8 : 0);
}

// The name of the member a class field or method declares (`key`: a computed name's type), null when it names none:
// only a literal or unique symbol key names a member (tsc's late bound names), a bigint name is the binder's __missing
static Atom const* class_item_prop_name(Checker* me, ClassItem const* item, cstr const* name_string, Symbol const* parent, Ty const** key) {
    Expr const* computed_key = item->tag == CLASS_ITEM_FIELD ? item->field.computed_key : item->method.computed_key;
    if (computed_key) {
        if (!computed_key_may_name(computed_key)) return nullptr;
        bool is_number = false;
        *key = ty_expr_type(me, computed_key);
        return ty_property_name_of_key(me, *key, &is_number);
    }
    if (span_valid(class_item_private_name(item)) && parent) return ty_private_name(me, parent, cstr_sv(name_string));
    if (item->tag == CLASS_ITEM_FIELD ? span_is_bigint_name(item->field.name_span, item->field.name_is_value) :
        span_is_bigint_name(item->method.name_span, item->method.name_is_value)
    ) {
        return nullptr;
    }
    return checker_atom(me, cstr_sv(name_string));
}

// The member of `class_symbol`'s type that a field or method of its declaration is a declaration of (the declarations
// the binder joins, the late-bound ones merged into them, accessor halves, overloads, an interface's members merged
// with the class: Prop.decl's spans, in tsc's order), null when it is none's (a declaration that conflicts with the
// symbol of its name stands alone, TS2300; a computed name that names nothing)
Prop const* ty_class_item_prop(Checker* me, Symbol const* class_symbol, ClassItem const* item) {
    if (!class_symbol || (item->tag != CLASS_ITEM_FIELD && item->tag != CLASS_ITEM_METHOD)) return nullptr;
    Ty const* key = nullptr;
    Atom const* name = class_item_prop_name(me, item, item->tag == CLASS_ITEM_FIELD ? &item->field.name_string : &item->method.name_string,
        class_symbol, &key);
    if (!name) return nullptr;
    Ty const* owner = class_item_is_static((ClassItem*)item) ? ty_static_type_of(me, class_symbol) : ty_declared_type_of(me, class_symbol);
    if (!owner || !(owner->flags & TYF_OBJECT)) return nullptr;
    Prop const* prop = ty_get_prop(me, owner, name);
    while (prop && prop->target && !prop->decl->decl_span_count) prop = prop->target; // (Of the instantiated class)
    if (!prop) return nullptr;
    for (u32 i = 0; i < prop->decl->decl_span_count; i++) {
        if (span_eq(prop->decl->decl_spans[i], item->span)) return prop;
    }
    return nullptr;
}

static Prop* builder_add_class_prop_joined(
    Checker* me, MembersBuilder* builder, ClassDecl const* class_decl, cstr const* name_string, Symbol const* parent,
    LowerCtx ctx, ClassItem const* item, Prop** joined
) {
    Expr const* computed_key = item->tag == CLASS_ITEM_FIELD ? item->field.computed_key : item->method.computed_key;
    if (computed_key && builder->skip_computed && class_item_is_late(item)) return nullptr;
    Ty const* key = nullptr;
    Atom const* name = class_item_prop_name(me, item, name_string, parent, &key);
    if (!name) return nullptr;
    Prop* existing = builder_find_prop(builder, name);
    if (existing) {
        // A member joins the symbol a declaration of the class before it made (ty_class_member_symbols(): the
        // binder's, the late-bound ones merged into the early ones), the first declaration's type. One that conflicted
        // with the symbol of its name, or a late-bound one the early symbol of its name excludes, stands alone
        ClassMemberSymbols const* symbols = ty_class_member_symbols(me, class_decl);
        u32 index = (u32)(item - class_decl->items.data);
        i32 entry = symbols->item_entry[index];
        bool joins;
        if (class_member_declared_before(symbols, entry, index, builder->only_late)) {
            joins = true;
        } else if (entry == CLASS_MEMBER_ALONE || (entry >= 0 && symbols->shadowed[entry])) {
            joins = false;
        } else if (builder->only_late) {
            // A late-bound member merges into the early symbol of its name, another declaration's (an interface's),
            // unless one excludes the other (tsc's combineSymbolTables: mergeSymbol, after the early members of
            // every declaration, see builder_add_declarations())
            joins = !(prop_meanings(existing) & meanings_excluded(class_item_meanings(item))) &&
                !(class_item_meanings(item) & meanings_excluded(prop_meanings(existing)));
        } else {
            // The first of its symbol in the class, after an interface's member merged with the class: a field or
            // an accessor after its property joins (the merged class and interface members are one table; a property
            // and an accessor merge, tsgo 7.1)
            bool accessor = (item->tag == CLASS_ITEM_FIELD && item->field.is_auto_accessor) ||
                (item->tag == CLASS_ITEM_METHOD && item->method.accessor);
            joins = (accessor || item->tag == CLASS_ITEM_FIELD) && !(existing->flags & (PROP_METHOD | PROP_ACCESSOR | PROP_FIELD)) &&
                !existing->decl->parameter_property && !existing->decl->function;
        }
        if (joins) prop_add_shared_decl_spans(me, existing, &item->span, 1);
        if (joins && joined) *joined = existing;
        return nullptr;
    }
    Prop* prop = prop_new_declared(me);
    prop->name = name;
    prop->parent = parent;
    prop->decl->ctx = ctx;
    prop->decl->name_span = item->tag == CLASS_ITEM_FIELD ? item->field.name_span : item->method.name_span;
    prop->decl->name_is_value = item->tag == CLASS_ITEM_FIELD ? item->field.name_is_value : item->method.name_is_value;
    if (computed_key) prop->decl->name_form = ty_prop_name_form(span_sv(prop->decl->name_span), computed_key, key, false);
    prop->flags = access_prop_flags(item->access) | (class_item_is_static((ClassItem*)item) ? PROP_STATIC : 0);
    prop_add_shared_decl_spans(me, prop, &item->span, 1);
    builder_push(builder, props, prop);
    return prop;
}

static Prop* builder_add_class_prop(
    Checker* me, MembersBuilder* builder, ClassDecl const* class_decl, cstr const* name_string, Symbol const* parent,
    LowerCtx ctx, ClassItem const* item
) {
    return builder_add_class_prop_joined(me, builder, class_decl, name_string, parent, ctx, item, nullptr);
}

// The members JavaScript assignments declare (`this.x = v`, `F.prototype.x = v`: Symbol.members). One the class body
// declares keeps its declaration and type, the assignments are more declarations of it
static void builder_add_assignment_members(Checker* me, MembersBuilder* builder, Symbol const* symbol) {
    if (!symbol_rare(symbol)->members) return;
    u32 count;
    SymbolEntry* entries = symbol_table_collect(symbol_rare(symbol)->members, &count);
    for (u32 i = 0; i < count; i++) {
        Symbol const* member = entries[i].symbol;
        Atom const* name;
        if (symbol_rare(member)->late_bound_key) {
            // (What the key's type names: a literal or unique symbol, else nothing)
            bool is_number = false;
            name = ty_property_name_of_key(me, ty_expr_type(me, symbol_rare(member)->late_bound_key), &is_number);
            if (!name) continue;
        } else {
            name = checker_atom(me, symbol_entry_name(&entries[i]));
        }
        VecSpan spans = symbol_declaration_spans(member);
        Prop* existing = builder_find_prop(builder, name);
        // `this.x = v` with an accessor x in the class body: tsgo's binder adds the Property meaning to the accessor's
        // symbol, whose type (the getter's) is then what is read and written, never readonly
        // (Only when an assignment comes first: one in the accessor's own body, bound after it, joins nothing, as the
        // baselines show)
        bool assigned_first = existing && existing->decl->decl_span_count && vec_span_size(&spans) &&
            span_source(spans.data[0]) == span_source(existing->decl->decl_spans[0]) &&
            span_start_ptr(spans.data[0]) < span_start_ptr(existing->decl->decl_spans[0]);
        if (existing && (existing->flags & PROP_ACCESSOR) && existing->decl->symbol != member && assigned_first) {
            existing->flags &= ~(u32)PROP_READONLY;
            if (prop_decl_rare(existing->decl)->write_node) prop_decl_rare_mut(me, existing->decl)->write_node = nullptr;
            // (One symbol: the assignments are declarations of it, in source order)
            u32 total = existing->decl->decl_span_count + (u32)vec_span_size(&spans);
            Span* all = arena_new_n(&my arena, Span, total);
            memcpy(all, existing->decl->decl_spans, sizeof(Span) * existing->decl->decl_span_count);
            memcpy(all + existing->decl->decl_span_count, spans.data, sizeof(Span) * vec_span_size(&spans));
            for (u32 a = 1; a < total; a++) {
                for (u32 b = a; b > 0 && span_source(all[b]) == span_source(all[b - 1]) && span_start_ptr(all[b]) < span_start_ptr(all[b - 1]); b--) {
                    Span t = all[b];
                    all[b] = all[b - 1];
                    all[b - 1] = t;
                }
            }
            existing->decl->decl_spans = all;
            existing->decl->decl_span_count = total;
        }
        // (One the class body declares has no assignment declarations: tsc's declareSymbol with isReplaceableByMethod)
        if (!existing) {
            Prop* prop = prop_new_declared(me);
            prop->name = name;
            prop->decl->symbol = member;
            prop->parent = symbol;
            prop->decl->ctx = (LowerCtx){ .generic = true };
            prop->decl->name_span = member->name_span;
            prop_add_decl_spans(me, prop, spans.data, (u32)vec_span_size(&spans));
            // `/** @private */ this.x = v` (tsc's getDeclarationModifierFlagsFromSymbol: the value declaration's)
            uint8_t modifiers = member->decl_count ? member->decls[0].jsdoc_modifiers : 0;
            if (modifiers & JSDOC_MODIFIER_PRIVATE) prop->flags |= PROP_PRIVATE;
            else if (modifiers & JSDOC_MODIFIER_PROTECTED) prop->flags |= PROP_PROTECTED;
            if (modifiers & JSDOC_MODIFIER_READONLY) prop->flags |= PROP_READONLY;
            // `this.x = v` declares an instance property: a class field to `super.x` (tsc's isClassInstanceProperty of an
            // expando property declaration)
            for (u32 d = 0; d < member->decl_count; d++) {
                if (member->decls[d].kind == SYMBOL_DECL_ASSIGNMENT && (member->decls[d].assignment == ASSIGNMENT_DECL_THIS_IN_CONSTRUCTOR ||
                    member->decls[d].assignment == ASSIGNMENT_DECL_THIS_IN_METHOD)
                ) {
                    prop->flags |= PROP_FIELD;
                }
            }
            if (member->decl_count && member->decls[0].assignment == ASSIGNMENT_DECL_DEFINE_PROPERTY &&
                define_property_readonly(me, member->decls[0].node)
            ) {
                prop->flags |= PROP_READONLY;
            }
            builder_push(builder, props, prop);
        } else if ((existing->flags & PROP_FIELD) && !(existing->flags & (PROP_METHOD | PROP_ACCESSOR)) &&
            existing->decl->decl_span_count && vec_span_size(&spans) &&
            span_source(spans.data[0]) == span_source(existing->decl->decl_spans[0]) &&
            span_start_ptr(spans.data[0]) < span_start_ptr(existing->decl->decl_spans[0])
        ) {
            // `this.x = v` in a constructor before the field `x` of the class body: one symbol (tsc's declareSymbol
            // makes the assignment's isReplaceableByMethod, which a property joins), declared in source order. After
            // the field, the assignments don't declare it. The field keeps its type
            u32 total = existing->decl->decl_span_count + (u32)vec_span_size(&spans);
            Span* all = arena_new_n(&my arena, Span, total);
            memcpy(all, existing->decl->decl_spans, sizeof(Span) * existing->decl->decl_span_count);
            memcpy(all + existing->decl->decl_span_count, spans.data, sizeof(Span) * vec_span_size(&spans));
            for (u32 a = 1; a < total; a++) {
                for (u32 b = a; b > 0 && span_source(all[b]) == span_source(all[b - 1]) && span_start_ptr(all[b]) < span_start_ptr(all[b - 1]); b--) {
                    Span t = all[b];
                    all[b] = all[b - 1];
                    all[b - 1] = t;
                }
            }
            existing->decl->decl_spans = all;
            existing->decl->decl_span_count = total;
        }
        vec_span_drop(&spans);
    }
    free(entries);
}

// A class member named by an expression of type string, number or symbol (not a literal or unique symbol, which name
// it) makes an index signature for that key of every member of the class (tsc's getIndexInfosOfIndexSymbol with
// hasLateBindableIndexSignature), unless the class declares one itself
// What a class member's computed name makes of it (tsc's hasLateBindableIndexSignature and the isSymbolWith*Name
// tests of getObjectLiteralIndexInfo): bit 0 its value goes into a string index signature, bit 1 a number one, bit 2 a
// symbol one; bit 3 the name makes that kind of index signature itself (string 0, number 1, symbol 2) in bits 4-5.
// 0 for a name that isn't late bindable as an index signature
static u32 computed_index_kind(Checker* me, ClassItem const* item, bool static_side) {
    bool field = item->tag == CLASS_ITEM_FIELD;
    if (!field && item->tag != CLASS_ITEM_METHOD) return 0;
    if ((field ? item->field.is_static : item->method.is_static) != static_side) return 0;
    Expr const* key = field ? item->field.computed_key : item->method.computed_key;
    if (!key || !expr_is_entity_name(key)) return 0;
    Ty const* key_type = ty_regular(me, ty_expr_type(me, key));
    // (A literal or unique symbol names a property, late bound: tsc's isLateBindableName comes first)
    if (key_type->flags & (TYF_LITERAL | TYF_UNIQUE_ES_SYMBOL | TYF_ENUM)) return 0;
    Ty const* keys[3] = { ty_intrinsic(me, TY_STRING), ty_intrinsic(me, TY_NUMBER), ty_intrinsic(me, TY_ES_SYMBOL) };
    if (!ty_is_related(me, key_type, ty_union(me, keys, 3, nullptr, (TyList){0}), TY_RELATION_ASSIGNABLE)) return 0;
    bool number = ty_is_related(me, key_type, keys[1], TY_RELATION_ASSIGNABLE);
    bool symbol = ty_is_related(me, key_type, keys[2], TY_RELATION_ASSIGNABLE);
    u32 makes = number ? 1 : symbol ? 2 : 0;
    return (symbol ? 0 : 1) | (number ? 2 : 0) | (symbol ? 4 : 0) | 8 | (makes << 4);
}

// A class member named by an expression of type string, number or symbol (not a literal or unique symbol, which name
// it; any counts as a number) makes an index signature for that key of every member of the class (tsc's
// getIndexInfosOfIndexSymbol with hasLateBindableIndexSignature), unless the class declares one itself
static void builder_add_computed_index_infos(Checker* me, MembersBuilder* builder, ClassDecl const* class_decl, bool static_side) {
    if (builder->skip_computed) return;
    static TyIntrinsic const keys[3] = { TY_STRING, TY_NUMBER, TY_ES_SYMBOL };
    bool computed[3] = {0}, readonly[3] = { true, true, true };
    for_each (i, vec_class_item, &class_decl->items) {
        u32 kind = computed_index_kind(me, i, static_side);
        if (!kind) continue;
        u32 k = kind >> 4;
        computed[k] = true;
        readonly[k] &= i->tag == CLASS_ITEM_FIELD && i->field.is_readonly;
    }
    for (u32 k = 0; k < 3; k++) {
        if (!computed[k]) continue;
        Ty const* key = ty_intrinsic(me, keys[k]);
        bool declared = false;
        for (u32 j = 0; j < builder->index_count; j++) declared |= builder->index[j].key == key;
        if (declared) continue;
        // The value is what every member of that kind of name has: the computed ones and the others (getObjectLiteralIndexInfo)
        TyBuf types = {0};
        for_each (i, vec_class_item, &class_decl->items) {
            u32 kind = computed_index_kind(me, i, static_side);
            if (!(kind & (1u << k))) continue;
            bool field = i->tag == CLASS_ITEM_FIELD;
            if (!field && i->method.accessor && i->method.accessor_first != i) continue; // One type per accessor pair
            Ty const* type = field || i->method.accessor ? class_item_get_type((ClassItem*)i) : ty_function_type(me, &i->method.func);
            // (The type of each member's symbol: an optional one's has undefined, getTypeOfSymbol)
            bool optional = field ? i->field.is_optional : i->method.is_optional;
            ty_buf_push(&types, optional ? ty_optional_prop_type(me, type) : type);
        }
        for (u32 j = 0; j < builder->props_count; j++) {
            Atom const* name = builder->props[j]->name;
            bool symbol_name = ty_unique_symbol_of_name(me, name) != nullptr;
            bool included = k == 0 ? !symbol_name : k == 1 ? ty_is_numeric_name(name) : symbol_name;
            // (The static side's `prototype` too: tsc's class exports have it, "all static string indexers include the
            // instance type")
            if (included) {
                Ty const* type = prop_type(me, builder->props[j]);
                ty_buf_push(&types, builder->props[j]->flags & PROP_OPTIONAL ? ty_optional_prop_type(me, type) : type);
            }
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
        // (The late-bound members in a pass of their own, see resolve_declared_members(); an interface member's own)
        if (builder->only_late && i->tag != CLASS_ITEM_INTERFACE_MEMBER && !class_item_is_late(i)) continue;
        switch (i->tag) {
            case CLASS_ITEM_FIELD: {
                if (i->field.is_static != static_side) break;
                Prop* joined = nullptr;
                Prop* prop = builder_add_class_prop_joined(me, builder, class_decl, &i->field.name_string, parent, ctx, i, &joined);
                // (An auto-accessor after a property of its name: one symbol, typed by the accessor, see the methods')
                if (!prop && joined && i->field.is_auto_accessor && (joined->flags & PROP_FIELD) &&
                    !(joined->flags & (PROP_ACCESSOR | PROP_METHOD))
                ) {
                    prop = joined;
                    prop->flags &= ~(u32)PROP_FIELD;
                    prop->decl->node = nullptr;
                    prop->thisless = false;
                }
                if (!prop) break;
                if (i->field.has_explicit_type) {
                    prop->decl->node = &i->field.explicit_type;
                } else {
                    prop->decl->lazy = &i->field.lazy_type;
                }
                prop->thisless = binding_is_thisless(i->field.has_explicit_type, &i->field.explicit_type,
                    i->field.value.tag != EXPR_IMPLICIT_ANY);
                if (!i->field.is_auto_accessor) prop->flags |= PROP_FIELD;
                else prop_decl_rare_mut(me, prop->decl)->class_auto_accessor = true;
                if (i->field.is_optional) prop->flags |= PROP_OPTIONAL;
                if (i->field.is_readonly) prop->flags |= PROP_READONLY;
            }   break;

            case CLASS_ITEM_METHOD: {
                if (i->method.is_static != static_side) break;
                if (i->method.accessor) {
                    if (i->method.accessor_first != i) break; // The second half of a get/set pair
                    Prop* joined = nullptr;
                    Prop* prop = builder_add_class_prop_joined(me, builder, class_decl, &i->method.name_string, parent, ctx, i, &joined);
                    // An accessor after a property of its name joins the property's symbol (tsgo 7.1's binder:
                    // PropertyExcludes and the accessors' excludes allow each other), which getTypeOfSymbol then types
                    // as accessors (getTypeOfAccessors: the property's declaration says nothing)
                    if (!prop && joined && (joined->flags & PROP_FIELD) && !(joined->flags & (PROP_ACCESSOR | PROP_METHOD))) {
                        prop = joined;
                        prop->flags &= ~(u32)PROP_FIELD;
                        prop->decl->node = nullptr;
                        prop->thisless = false;
                    }
                    if (!prop) break;
                    prop->decl->lazy = &i->method.accessor_type;
                    prop->flags |= PROP_ACCESSOR;
                    // Reads go by the getter's accessibility, writes by the setter's
                    bool has_setter = false, has_getter = false;
                    TypeNode const* setter_type = nullptr;
                    u32 get_access = access_prop_flags(i->access), set_access = get_access;
                    for (ClassNamesakes it = class_namesakes(class_decl, i, false); class_namesakes_next(&it);) {
                        ClassItem const* other = it.item;
                        if (other->tag != CLASS_ITEM_METHOD || other->method.accessor_first != i) continue;
                        has_setter |= other->method.accessor == CLASS_ACCESSOR_SET;
                        has_getter |= other->method.accessor == CLASS_ACCESSOR_GET;
                        FunctionImpl const* func = &other->method.func;
                        if (other->method.accessor == CLASS_ACCESSOR_SET && vec_function_arg_size(&func->args) &&
                            func->args.data[0].initializer.binding.has_explicit_type
                        ) {
                            setter_type = binding_type(&func->args.data[0].initializer.binding);
                        }
                        if (other != i) prop_add_shared_decl_spans(me, prop, &other->span, 1);
                        if (other->method.accessor == CLASS_ACCESSOR_GET) get_access = access_prop_flags(other->access);
                        if (other->method.accessor == CLASS_ACCESSOR_SET) set_access = access_prop_flags(other->access);
                    }
                    if (!has_setter) set_access = get_access;
                    prop->flags = (prop->flags & ~(u32)(PROP_PRIVATE | PROP_PROTECTED)) | get_access |
                        (set_access & PROP_PRIVATE ? PROP_SET_PRIVATE : 0) | (set_access & PROP_PROTECTED ? PROP_SET_PROTECTED : 0);
                    if (!has_setter) prop->flags |= PROP_READONLY;
                    if (has_getter) prop->flags |= PROP_CLASS_GETTER;
                    for (ClassNamesakes it = class_namesakes(class_decl, i, false); class_namesakes_next(&it);) {
                        ClassItem const* other = it.item;
                        if (other->tag != CLASS_ITEM_METHOD || other->method.accessor_first != i) continue;
                        PropDeclRare* rare = prop_decl_rare_mut(me, prop->decl);
                        if (other->method.accessor == CLASS_ACCESSOR_GET && !rare->class_getter) rare->class_getter = &other->method.func;
                        if (other->method.accessor == CLASS_ACCESSOR_SET && !rare->class_setter) rare->class_setter = &other->method.func;
                    }
                    if (has_getter) prop_decl_rare_mut(me, prop->decl)->write_node = setter_type; // Writes go by the setter's parameter type
                    break;
                }
                u32 visible = 0;
                for (ClassNamesakes it = class_namesakes(class_decl, i, false); class_namesakes_next(&it);) {
                    ClassItem const* other = it.item;
                    if (other->tag != CLASS_ITEM_METHOD || other->method.accessor || other->method.is_static != static_side ||
                        !class_methods_same_name(other, i)
                    ) {
                        continue;
                    }
                    if (!visible && other != i) goto next_item; // Not the first declaration of this method
                    visible++;
                }
                {
                    FunctionImpl const** functions = arena_new_n(&my arena, FunctionImpl const*, visible);
                    u32 count = 0;
                    for (ClassNamesakes it = class_namesakes(class_decl, i, false); class_namesakes_next(&it);) {
                        ClassItem const* other = it.item;
                        ClassItem const* before = it.index ? other - 1 : nullptr; // The class item before `other`
                        if (other->tag != CLASS_ITEM_METHOD || other->method.accessor || other->method.is_static != static_side ||
                            !class_methods_same_name(other, i)
                        ) {
                            continue;
                        }
                        // A body right after a declaration of the same method is its implementation, even after another
                        // body (tsc's getSignaturesOfSymbol: `m() {} m(x) {}` has one signature). One that isn't (the
                        // static `m` between `m(x: string);` and `m(x) {}`) is a signature of its own
                        if (count && other->method.func.has_body && before && before->tag == CLASS_ITEM_METHOD &&
                            !before->method.accessor && before->method.is_static == static_side &&
                            class_methods_same_name(before, i) &&
                            adjacent_methods(before, other)
                        ) {
                            continue;
                        }
                        functions[count++] = &other->method.func;
                    }
                    // JSDoc `@overload`s instead of the method's own signature
                    if (count == 1 && function_impl_rare(functions[0])->jsdoc_overload_count) {
                        FunctionImpl const* method = functions[0];
                        count = function_impl_rare(method)->jsdoc_overload_count;
                        functions = arena_new_n(&my arena, FunctionImpl const*, count);
                        for (u32 f = 0; f < count; f++) functions[f] = function_impl_rare(method)->jsdoc_overloads[f].callable->function;
                    }
                    u32 span_cap = visible;
                    Span* spans = arena_new_n(&my arena, Span, span_cap);
                    u32 span_count = 0;
                    for (ClassNamesakes it = class_namesakes(class_decl, i, false); class_namesakes_next(&it);) {
                        ClassItem const* other = it.item;
                        if (other != i && other->tag == CLASS_ITEM_METHOD && !other->method.accessor &&
                            other->method.is_static == static_side && class_methods_same_name(other, i)
                        ) {
                            spans[span_count++] = other->span;
                        }
                    }
                    if (!count) break;
                    // After an interface's method of its name (the class merges with it): one method, the interface's
                    // signatures then the class's (tsc's getSignaturesOfSymbol over the symbol's declarations)
                    // A late-bound method after the early method of its name (the early declarations of every
                    // declaration first): one method too, the early signatures then the late ones (mergeSymbol)
                    Prop* interface_method = !i->method.computed_key && !span_valid(class_item_private_name(i)) ?
                        builder_find_prop(builder, checker_atom(me, cstr_sv(&i->method.name_string))) : nullptr;
                    if (builder->only_late) {
                        interface_method = nullptr;
                        Atom const* late_name = ty_member_name(me, &i->method.name_string, i->method.computed_key);
                        Prop* early = late_name ? builder_find_prop(builder, late_name) : nullptr;
                        if (early && (early->flags & PROP_METHOD) &&
                            (early->decl->node ? early->decl->node->tag == TYPE_CALLABLE : early->decl->function != nullptr)
                        ) {
                            interface_method = early;
                        }
                    }
                    if (interface_method && (interface_method->flags & PROP_METHOD) &&
                        (builder->only_late || (interface_method->decl->node && interface_method->decl->node->tag == TYPE_CALLABLE))
                    ) {
                        u32 had = prop_decl_rare(interface_method->decl)->overload_count ? prop_decl_rare(interface_method->decl)->overload_count : 1;
                        FunctionImpl const** all = arena_new_n(&my arena, FunctionImpl const*, had + count);
                        if (prop_decl_rare(interface_method->decl)->overload_count) {
                            memcpy(all, prop_decl_rare(interface_method->decl)->overloads, sizeof(FunctionImpl const*) * had);
                        } else {
                            all[0] = interface_method->decl->node ? interface_method->decl->node->callable->function :
                                interface_method->decl->function;
                        }
                        memcpy(all + had, functions, sizeof(FunctionImpl const*) * count);
                        prop_decl_rare_mut(me, interface_method->decl)->overloads = all;
                        prop_decl_rare_mut(me, interface_method->decl)->overload_count = had + count;
                        for (u32 f = 0; f < count; f++) interface_method->thisless &= function_is_thisless(functions[f]);
                        prop_add_shared_decl_spans(me, interface_method, &i->span, 1);
                        prop_add_decl_spans(me, interface_method, spans, span_count);
                        break;
                    }
                    LowerCtx method_ctx = ctx;
                    method_ctx.method = true;
                    Prop* prop = builder_add_class_prop(me, builder, class_decl, &i->method.name_string, parent, method_ctx, i);
                    if (!prop) break;
                    prop->decl->function = functions[0];
                    prop->flags |= PROP_METHOD;
                    prop->thisless = true;
                    for (u32 f = 0; f < count; f++) prop->thisless &= function_is_thisless(functions[f]);
                    prop_add_shared_decl_spans(me, prop, spans, span_count);
                    add_jsdoc_overload_decl_spans(me, prop, class_decl, i, static_side);
                    if (i->method.is_optional) prop->flags |= PROP_OPTIONAL;
                    if (count > 1) {
                        prop_decl_rare_mut(me, prop->decl)->overloads = functions;
                        prop_decl_rare_mut(me, prop->decl)->overload_count = count;
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
                    Atom const* name = checker_atom(me, span_sv(binding->content_span));
                    Prop* existing = builder_find_prop(builder, name);
                    if (existing) {
                        // A member declared before it: one symbol (tsc's PropertyExcludes is properties and accessors),
                        // the member's type (and the same parameter of another constructor declaration, an overload's:
                        // TS2369)
                        ClassMemberSymbols const* symbols = ty_class_member_symbols(me, class_decl);
                        i32 entry = CLASS_MEMBER_NONE;
                        for (u32 p = 0; p < symbols->param_count; p++) {
                            if (symbols->params[p].arg == arg) entry = symbols->params[p].entry;
                        }
                        if (class_member_declared_before(symbols, entry, (u32)i_index, false)) {
                            prop_add_shared_decl_spans(me, existing, &arg->span, 1);
                        }
                        continue;
                    }
                    Prop* prop = prop_new_declared(me);
                    prop->name = name;
                    prop->parent = parent;
                    prop->decl->ctx = ctx;
                    prop->flags = access_prop_flags(arg->access);
                    prop->decl->name_span = binding->content_span;
                    prop->decl->parameter_property = true;
                    prop_add_shared_decl_spans(me, prop, &arg->span, 1);
                    if (binding->has_explicit_type) {
                        prop->decl->node = binding_type(binding);
                    } else {
                        prop->decl->lazy = &arg->initializer.lazy_type;
                    }
                    prop->thisless = binding_is_thisless(binding->has_explicit_type, binding_type(binding),
                        initialized_binding_value(&arg->initializer)->tag != EXPR_IMPLICIT_ANY);
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
    // A symbol's declarations are in binding order: an accessor pair's spans went in at its first accessor, a property
    // between the two (`get x(); x: number; set x(v)`, one symbol) after them. Put back those within the class (not
    // others: late-bound declarations come after the early ones, see resolve_declared_members())
    for (u32 j = 0; j < builder->props_count; j++) {
        Prop* prop = builder->props[j];
        u32 count = prop->decl->decl_span_count;
        if (count < 3 || !(prop->flags & PROP_ACCESSOR) || prop->decl == prop_decl_none()) continue;
        Span const* spans = prop->decl->decl_spans;
        bool inside = true, sorted = true;
        for (u32 k = 0; k < count && inside; k++) {
            inside = span_source(spans[k]) == span_source(class_decl->full_span) && span_contains_span(class_decl->full_span, spans[k]);
            if (k) sorted &= spans[k - 1].buf <= spans[k].buf;
        }
        if (!inside || sorted) continue;
        Span* copy = arena_new_n(&my arena, Span, count);
        for (u32 k = 0; k < count; k++) {
            u32 at = k;
            while (at && copy[at - 1].buf > spans[k].buf) {
                copy[at] = copy[at - 1];
                at--;
            }
            copy[at] = spans[k];
        }
        prop->decl->decl_spans = copy;
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
    Prop* prototype = prop_new(me);
    prototype->name = checker_atom(me, c_sv("prototype"));
    prototype->parent = instance_decl;
    prototype->flags = PROP_STATIC | PROP_PROTOTYPE; // Not readonly: tsc's prototype symbol is a plain property
    if (instance->declared.type_params.len) {
        u32 len = instance->declared.type_params.len;
        Ty const** args = arena_new_n(&my arena, Ty const*, len);
        for (u32 i = 0; i < len; i++) args[i] = ty_intrinsic(me, TY_ANY);
        prototype->type = ty_reference(me, instance, (TyList){ .data = args, .len = len });
    } else {
        prototype->type = instance;
    }
    prototype->type_state = LAZY_STATE_DONE;
    // (First, so that it wins over a declared `static prototype`; builder_prototype_last() moves it)
    builder_push(builder, props, prototype);

    if (builder->skip_computed) {
        builder_add_class_items(me, builder, class_decl, true, ctx, instance_decl);
    } else {
        // The early-bound members, then the late-bound ones (see resolve_declared_members())
        builder->skip_computed = true;
        builder_add_class_items(me, builder, class_decl, true, ctx, instance_decl);
        builder->skip_computed = false;
        builder->only_late = true;
        builder_add_class_items(me, builder, class_decl, true, ctx, instance_decl);
        builder->only_late = false;
    }
    // A static member and a type the class's namespace exports of its name are one symbol in tsc (both in the class's
    // exports): symbols baseline only, the type stays a type. Same for an expando assignment of a static property
    if (instance_decl->exports) {
        for (u32 i = 0; i < builder->props_count; i++) {
            Prop* prop = builder->props[i];
            if (prop == prototype || !(prop->flags & PROP_STATIC)) continue;
            Symbol const* exported = symbol_table_get(instance_decl->exports, atom_sv(prop->name));
            // (An expando assignment `C.x = v` too, a property, which a static property joins: not a method)
            u32 value = exported ? exported->flags & SYMBOL_VALUE : 0;
            if (!exported || !exported->decl_count || (value && (value != SYMBOL_PROPERTY || (prop->flags & PROP_METHOD)))) continue;
            VecSpan spans = symbol_declaration_spans(exported);
            prop_add_decl_spans(me, prop, spans.data, (u32)vec_span_size(&spans));
            vec_span_drop(&spans);
            // (In the order the binder met them: the namespace may come first. The list was just made)
            Span* all = (Span*)prop->decl->decl_spans;
            for (u32 a = 1; a < prop->decl->decl_span_count; a++) {
                for (u32 b = a; b > 0 && span_source(all[b]) == span_source(all[b - 1]) && span_start_ptr(all[b]) < span_start_ptr(all[b - 1]); b--) {
                    Span t = all[b];
                    all[b] = all[b - 1];
                    all[b - 1] = t;
                }
            }
        }
    }

    // Construct signatures: the constructor's overload signatures, else its implementation, else the base class's
    bool has_constructor = false, has_overloads = false;
    for_each (i, vec_class_item, &class_decl->items) {
        if (i->tag != CLASS_ITEM_CONSTRUCTOR) continue;
        has_constructor = true;
        has_overloads |= !i->constructor.func.has_body;
    }
    for_each (i, vec_class_item, &class_decl->items) {
        if (i->tag != CLASS_ITEM_CONSTRUCTOR || (has_overloads && i->constructor.func.has_body)) continue;
        FunctionImplRare const* rare = function_impl_rare(&i->constructor.func);
        if (rare->jsdoc_overload_count && !has_overloads) {
            // JSDoc `@overload`s instead of the constructor's own signature, each making the instance type (tsc's
            // getReturnTypeFromAnnotation of a JSDocSignature whose root is on a constructor)
            for (u32 f = 0; f < rare->jsdoc_overload_count; f++) {
                if (rare->jsdoc_overloads[f].tag != TYPE_CALLABLE) continue;
                FunctionImpl const* overload = rare->jsdoc_overloads[f].callable->function;
                Signature* sig = class_construct_signature(me, class_decl, instance);
                sig->function = overload;
                sig->name_span = overload->full_span;
                sig->decl_span = overload->full_span;
                builder_push(builder, construct, sig);
            }
            continue;
        }
        Signature* sig = class_construct_signature(me, class_decl, instance);
        sig->function = &i->constructor.func;
        sig->name_span = i->constructor.name_span;
        sig->decl_span = i->constructor.func.full_span;
        builder_push(builder, construct, sig);
    }

    // The base's type arguments can mention the static side (`class S extends B<typeof S.Base>`): tsc has published
    // its properties by then (resolveAnonymousTypeMembers sets them before the default construct signatures)
    TyObjectState* state = object_state(ty);
    if (class_decl->extends.tag == EXPR_GENERIC_PARAMS && state && state->members_state == LAZY_STATE_IN_FLIGHT &&
        !state->members
    ) {
        Members* partial = arena_new(&my arena, Members);
        Prop** props = arena_new_n(&my arena, Prop*, builder->props_count ? builder->props_count : 1);
        memcpy(props, builder->props, sizeof(Prop*) * builder->props_count);
        partial->props = props;
        partial->prop_count = builder->props_count;
        state->members = partial;
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
            // tsc's getDefaultConstructSignatures: the base constructor's signatures that take the written type
            // arguments, returning this class
            Expr const* extends = &class_decl->extends;
            VecSpannedType const* written = extends->tag == EXPR_GENERIC_PARAMS ? &extends->generic_params->params : nullptr;
            for (u32 i = 0; i < expr_members->construct_count; i++) {
                Signature const* base_sig = expr_members->construct[i];
                TyMapper const* mapper;
                if (!base_signature_type_args(me, base_sig, written, ctx, &mapper)) continue;
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
    } else if (base_expr_constructor && (base_expr_constructor->kind == TY_INTERSECTION ||
        ty_members(me, base_expr_constructor)->construct_count)
    ) {
        // (A constructor whose instance type is no valid base, the error type of a broken construct signature: its
        // properties are still the static side's, tsc's resolveAnonymousTypeMembers goes by getBaseConstructorTypeOfClass)
        base_constructor = base_expr_constructor;
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

// A class whose static members have computed names: while their keys are evaluated, the static side already has the
// members it names syntactically (tsc's getResolvedMembersOrExportsOfSymbol publishes the early-bound symbols before
// late binding), so `static [A.p1] = 0` finds `A.p1`
static void early_static_members(Checker* me, Ty const* ty, ClassDecl const* class_decl) {
    bool computed = false;
    for_each (i, vec_class_item, &class_decl->items) {
        if (i->tag == CLASS_ITEM_FIELD && i->field.is_static && i->field.computed_key) computed = true;
        if (i->tag == CLASS_ITEM_METHOD && i->method.is_static && i->method.computed_key) computed = true;
    }
    TyObjectState* state = object_state(ty);
    if (!computed || !state || state->members_state != LAZY_STATE_IN_FLIGHT || state->members) return;
    MembersBuilder early = { .skip_computed = true };
    builder_add_class_static_side(me, &early, ty, class_decl);
    state->members = builder_finish(me, &early);
}

static Prop* namespace_export_prop(Checker* me, SymbolEntry const* entry);

// A type-only export of a namespace or module (`export type { A }`, or reached through `export type *`)
static bool namespace_export_is_type_only(Symbol const* namespace, SymbolEntry const* entry) {
    return symbol_type_only_declaration(entry->symbol) ||
        module_type_only_star((Symbol*)symbol_members(namespace), symbol_entry_name(entry));
}

Prop const* ty_namespace_type_only_prop(Checker* me, Ty const* object, Atom const* name) {
    if (object->kind != TY_DECLARED || object->declared.kind != TY_DECLARED_NAMESPACE) return nullptr;
    Symbol const* namespace = object->declared.namespace_symbol;
    csview sv = atom_sv(name);
    Symbol* symbol = symbol_get_export(namespace, sv);
    if (!symbol) return nullptr;
    SymbolEntry entry = symbol_entry_make(sv, symbol_name_hash(sv), symbol);
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
    if (!member) return nullptr;
    Atom const* name = checker_atom(me, symbol_entry_name(entry));
    if (symbol_rare(symbol)->late_bound_key) {
        // `f[k] = v` (What the key's type names: a literal or unique symbol, else nothing)
        bool is_number = false;
        name = ty_property_name_of_key(me, ty_expr_type(me, symbol_rare(symbol)->late_bound_key), &is_number);
        if (!name) return nullptr;
    }
    Prop* prop = prop_new_declared(me);
    prop->name = name;
    prop->decl->symbol = member;
    // tsc's isReadonlySymbol: a `const` variable. Not through an alias (`export { c }`, `export default c`, `export
    // import c = require(...)`): the property is the alias symbol, which has no Variable flag
    if (member->is_const && member == symbol) prop->flags |= PROP_READONLY;
    if (member != symbol && (symbol->flags & SYMBOL_ALIAS)) prop->flags |= PROP_NO_VALUE_DECLARATION;
    // (`Object.defineProperty(o, "x", { value: 1 })`: isReadonlyAssignmentDeclaration)
    if (member->decl_count && member->decls[0].kind == SYMBOL_DECL_ASSIGNMENT &&
        member->decls[0].assignment == ASSIGNMENT_DECL_DEFINE_PROPERTY && define_property_readonly(me, member->decls[0].node)
    ) {
        prop->flags |= PROP_READONLY;
    }
    prop->decl->ctx = (LowerCtx){ .generic = true };
    prop->decl->name_span = symbol->name_span;
    // Where it is declared is its valueDeclaration: the first declaration with a value, not a type-only one before it (an
    // uninstantiated namespace block, an interface merged with a class: tsc's setValueDeclaration)
    if (member == symbol && symbol->decl_count && !(symbol->decls[0].flags & SYMBOL_VALUE)) {
        for (u32 i = 1; i < symbol->decl_count; i++) {
            if (!(symbol->decls[i].flags & SYMBOL_VALUE) || !span_valid(symbol->decls[i].name)) continue;
            prop->decl->name_span = symbol->decls[i].name;
            break;
        }
    }
    // (Named by its key's type: `f[s] = v` with `s = "10"` is string named, `"10"`)
    if (symbol_rare(symbol)->late_bound_key) {
        prop->decl->name_form = ty_prop_name_form(span_sv(prop->decl->name_span), symbol_rare(symbol)->late_bound_key,
            ty_expr_type(me, symbol_rare(symbol)->late_bound_key), false);
    }
    // `N.p` in the symbols baseline
    if (symbol->parent && !symbol->parent->is_module) prop->parent = symbol->parent;
    VecSpan spans = symbol_declaration_spans(symbol);
    // (A `{}` container's expando merged with a dotted typedef's namespace: the expando's own, SymbolRare.literal_member)
    if (symbol_rare(symbol)->literal_member && member == symbol) {
        prop->parent = nullptr;
        SymbolDecl* decls = malloc(sizeof(SymbolDecl) * (symbol->decl_count ? symbol->decl_count : 1));
        panic_if(!decls, "out of memory");
        u32 len = 0;
        for (u32 i = 0; i < symbol->decl_count; i++) {
            if (symbol->decls[i].kind != SYMBOL_DECL_NAMESPACE) decls[len++] = symbol->decls[i];
        }
        if (len != symbol->decl_count) {
            Symbol own = *symbol;
            own.decls = decls;
            own.decl_count = len;
            vec_span_drop(&spans);
            spans = symbol_declaration_spans(&own);
        }
        free(decls);
    }
    prop_add_decl_spans(me, prop, spans.data, (u32)vec_span_size(&spans));
    vec_span_drop(&spans);
    return prop;
}

// The members of every value declaration of a merged declaration, see ty_merged_value_type()
static void builder_add_merged_value(Checker* me, MembersBuilder* builder, Ty const* ty, Symbol const* decl) {
    Ty const** entries;
    u32 entry_count = ty_merged_value_entries(me, decl, &entries);
    // A class whose base's type arguments mention the merged value (`class S extends B<typeof S.Base>` with `namespace
    // S { export const Base }`): its namespace's exports are already there for them (tsc's class symbol has them among
    // its exports, published before the base is resolved)
    TyObjectState* state = object_state(ty);
    if (state && state->members_state == LAZY_STATE_IN_FLIGHT && !state->members && entry_count > 1) {
        bool generic_base = false;
        for (u32 i = 0; i < decl->decl_count; i++) {
            if (decl->decls[i].kind != SYMBOL_DECL_CLASS) continue;
            generic_base |= ((ClassDecl const*)decl->decls[i].node)->extends.tag == EXPR_GENERIC_PARAMS;
        }
        if (generic_base) {
            MembersBuilder early = {0};
            for (u32 i = 0; i < entry_count; i++) {
                Ty const* entry = entries[i];
                if (entry->kind != TY_DECLARED || entry->declared.kind != TY_DECLARED_NAMESPACE) continue;
                Members const* members = ty_members(me, entry);
                for (u32 j = 0; j < members->prop_count; j++) builder_push(&early, props, (Prop*)members->props[j]);
            }
            state->members = builder_finish(me, &early);
        }
    }
    // A property only one entry has is that entry's own (an inherited static stays the base's, whose private and
    // protected ones are only related to themselves); it's copied once another entry adds to it
    TyBuf shared = {0};
    // (Of the declaration groups below: the entries' signatures come in the order of their declarations, the search
    // starts next to the last one found; each declaration's node is its own)
    u32 cursor = 0;
    for (u32 entry_index = 0; entry_index < entry_count; entry_index++) {
        Ty const* entry = entries[entry_index];
        if (!(entry->flags & TYF_OBJECT)) continue;
        Members const* members = ty_members(me, entry);
        for (u32 j = 0; j < members->prop_count; j++) {
            Prop* existing = builder_find_prop(builder, members->props[j]->name);
            bool is_shared = false;
            for (u32 k = 0; existing && k < shared.len && !is_shared; k++) is_shared = (Prop const*)(void const*)shared.data[k] == existing;
            if (is_shared) {
                Prop* copy = arena_new(&my arena, Prop);
                prop_copy_into(me, copy, existing);
                for (u32 k = 0; k < builder->props_count; k++) if (builder->props[k] == existing) builder->props[k] = copy;
                existing = copy;
            }
            // (Unless the export is an alias, `export import X = ...`, which has no value flags of its own: the inherited
            // static stays, tsgo's addInheritedMembers replaces a member without SymbolFlagsValue)
            Symbol const* own_export = existing && existing->parent && existing->parent != decl ?
                symbol_get_export(decl, atom_sv(members->props[j]->name)) : nullptr;
            if (own_export && (own_export->flags & SYMBOL_ALIAS) && !(own_export->flags & SYMBOL_VALUE)) continue;
            if (existing && existing->parent && existing->parent != decl && members->props[j]->parent == decl) {
                // The symbol's own export (a JavaScript class's expando `C.x = v`) hides a static the class inherits
                // (tsc: the class's exports come before the base constructor's members). The props here are copies
                prop_copy_into(me, existing, members->props[j]);
                if (existing->type_state != LAZY_STATE_DONE) existing->type_state = LAZY_STATE_NOT_STARTED;
            } else if (existing) {
                prop_add_shared_decl_spans(me, existing, members->props[j]->decl->decl_spans, members->props[j]->decl->decl_span_count);
            } else {
                Prop* prop = (Prop*)members->props[j];
                ty_buf_push(&shared, (Ty const*)(void const*)prop);
                builder_push(builder, props, prop);
            }
        }
        for (u32 j = 0; j < members->call_count; j++) builder_push(builder, call, members->call[j]);
        for (u32 j = 0; j < members->construct_count; j++) builder_push(builder, construct, members->construct[j]);
        for (u32 j = 0; j < members->index_count; j++) builder_push(builder, index, members->index[j]);
        // A function declared in several places (lib.dom's `setTimeout` and @types/node's `declare global` one): each
        // place's overloads are a group, and overload resolution tries the later groups first (reorder_candidates)
        // (tsc's signature.declaration.parent: the block the declaration is in)
        for (u32 j = 0; entry_count > 1 && j < members->call_count; j++) {
            FunctionImpl const* function = members->call[j]->function;
            if (!function || ty_decl_group(me, function)) continue;
            void const* parent = entry;
            u32 count = decl->decl_count;
            u32 found = UINT32_MAX;
            if (cursor + 1 < count && decl->decls[cursor + 1].node == function) found = cursor + 1;
            else if (cursor > 0 && cursor - 1 < count && decl->decls[cursor - 1].node == function) found = cursor - 1;
            for (u32 d = 0; found == UINT32_MAX && d < count; d++) {
                if (decl->decls[d].node == function) found = d;
            }
            if (found != UINT32_MAX) {
                if (decl->decls[found].block) parent = decl->decls[found].block;
                cursor = found;
            }
            set_decl_group(me, function, decl_group_of(me, decl, parent));
        }
    }
    free(entries);
    ty_buf_drop(&shared);
}

static void builder_add_enum_object(Checker* me, MembersBuilder* builder, EnumDecl const* enum_decl) {
    // (An enum without members is tsc's computed enum type, TypeFlags.Enum: it has the reverse mapping too)
    bool any_numeric = !vec_enum_case_size(&enum_decl->cases) && enum_decl->symbol && enum_decl->symbol->decl_count == 1;
    for_each (i, vec_enum_case, &enum_decl->cases) {
        if (!i->symbol) continue;
        Atom const* name = checker_atom(me, span_sv(i->name));
        if (builder_find_prop(builder, name)) continue;
        Prop* prop = prop_new_declared(me);
        prop->name = name;
        prop->parent = enum_decl->symbol;
        prop->flags = PROP_READONLY;
        prop->type = ty_declared_type_of(me, i->symbol);
        prop->type_state = LAZY_STATE_DONE;
        prop->decl->name_span = i->full_name;
        prop->decl->name_is_value = i->name_is_value;
        prop->decl->symbol = i->symbol;
        if (i->symbol->decl_count > 1) {
            // (JavaScript `E.x = v` merged into the member: more declarations, tsc's mergeSymbol with Assignment)
            VecSpan spans = symbol_declaration_spans(i->symbol);
            prop_add_decl_spans(me, prop, spans.data, (u32)vec_span_size(&spans));
            vec_span_drop(&spans);
        } else {
            prop_add_shared_decl_spans(me, prop, &i->full_name, 1);
        }
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
// Type arguments don't matter. The declarations already walked are skipped: mixin chains whose interfaces each extend
// the last several classes reach the same bases along exponentially many paths (tsgo's
// intersectionConstructorReductionCrash), and a circular hierarchy ends there too
typedef struct BaseTypeWalk {
    Ty const* target;
    Ty const** visited;
    u32 len, cap;
} BaseTypeWalk;

static bool has_base_type_walk(Checker* me, BaseTypeWalk* walk, Ty const* source) {
    if (source->kind == TY_REFERENCE) source = source->reference.target;
    if (source == walk->target) return true;
    if (source->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < source->composite.types.len; i++) {
            if (has_base_type_walk(me, walk, source->composite.types.data[i])) return true;
        }
        return false;
    }
    if (source->kind != TY_DECLARED) return false;
    if (source->declared.kind != TY_DECLARED_INTERFACE && source->declared.kind != TY_DECLARED_CLASS_INSTANCE) return false;
    Symbol const* decl = source->declared.symbol;
    if (!decl) return false;
    for (u32 i = 0; i < walk->len; i++) {
        if (walk->visited[i] == source) return false;
    }
    if (walk->len == walk->cap) {
        walk->cap = walk->cap ? walk->cap * 2 : 16;
        walk->visited = realloc(walk->visited, sizeof(Ty const*) * walk->cap);
        panic_if(!walk->visited, "out of memory");
    }
    walk->visited[walk->len++] = source;
    LowerCtx ctx = { .generic = source->declared.type_params.len > 0 };
    for (SymbolDecl const* node = decl->decls; node < decl->decls + decl->decl_count; node++) {
        if (node->kind == SYMBOL_DECL_CLASS) {
            Ty const* base = nullptr;
            Symbol const* base_decl = nullptr;
            if (class_base(me, node->node, ctx, &base, &base_decl) && base != source && has_base_type_walk(me, walk, base)) {
                return true;
            }
        } else if (node->kind == SYMBOL_DECL_INTERFACE) {
            InterfaceDecl const* interface_decl = node->node;
            for_each (extends, vec_spanned_type, &interface_decl->extends) {
                Ty const* base = ty_from_node(me, &extends->type, ctx);
                if (base != source && has_base_type_walk(me, walk, base)) return true;
            }
        }
    }
    return false;
}

static bool has_base_type(Checker* me, Ty const* source, Ty const* target) {
    BaseTypeWalk walk = { .target = target };
    bool ret = has_base_type_walk(me, &walk, source);
    free(walk.visited);
    return ret;
}

// Whether a class or interface declares members of its own (tsc's getMembersOfSymbol(symbol).size: the instance side,
// the constructor included)
static bool declares_members(Symbol const* decl) {
    if (symbol_rare(decl)->members && symbol_rare(decl)->members->count) return true;
    for (SymbolDecl const* node = decl->decls; node < decl->decls + decl->decl_count; node++) {
        if (declaration_has_members(node)) return true;
        if (node->kind == SYMBOL_DECL_INTERFACE) {
            InterfaceDecl const* interface_decl = node->node;
            if (vec_interface_member_size(&interface_decl->type.interface.members)) return true;
        } else if (node->kind == SYMBOL_DECL_CLASS) {
            ClassDecl const* class_decl = node->node;
            for_each (i, vec_class_item, &class_decl->items) {
                switch (i->tag) {
                    case CLASS_ITEM_CONSTRUCTOR: return true;
                    case CLASS_ITEM_METHOD: if (!i->method.is_static) return true; break;
                    case CLASS_ITEM_FIELD: if (!i->field.is_static) return true; break;
                    case CLASS_ITEM_INTERFACE_MEMBER: if (!i->interface_member_static) return true; break;
                    case CLASS_ITEM_STATIC_BLOCK: break;
                }
            }
        }
    }
    return false;
}

// Whether tsc makes the declared type of a class or interface a TypeReference (ObjectFlags.Reference): a class, a
// generic one, or an interface that isn't thisless (mentions `this`, or extends something other than an interface or
// an interface that has a `this` type: tsc's isThislessInterface)
static bool is_tsc_reference(Checker* me, Ty const* declared, u32 depth) {
    if (declared->declared.kind == TY_DECLARED_CLASS_INSTANCE || declared->declared.type_params.len) return true;
    if (declared->declared.this_param) return true;
    Symbol const* symbol = declared->declared.symbol;
    if (!symbol || depth > 16) return false;
    for (SymbolDecl const* node = symbol->decls; node < symbol->decls + symbol->decl_count; node++) {
        if (node->kind != SYMBOL_DECL_INTERFACE) continue;
        InterfaceDecl const* interface_decl = node->node;
        for_each (extends, vec_spanned_type, &interface_decl->extends) {
            if (extends->type.tag != TYPE_IDENTIFIER || identifier_type_import_ref(extends->type.identifier)) continue;
            Symbol const* base = extends->type.identifier->target;
            if (!base || base == symbol || !(base->flags & SYMBOL_INTERFACE)) continue;
            Ty const* base_declared = ty_declared_type_of(me, base);
            if (base_declared->kind == TY_DECLARED && is_tsc_reference(me, base_declared, depth + 1)) return true;
        }
    }
    return false;
}

Ty const* ty_single_base_for_non_augmenting_subtype(Checker* me, Ty const* ty) {
    Ty const* declared = ty->kind == TY_REFERENCE && !ty->reference.deferred ? ty->reference.target : ty;
    if (declared->kind != TY_DECLARED || declared->declared.merged_value) return nullptr;
    if (declared->declared.kind != TY_DECLARED_INTERFACE && declared->declared.kind != TY_DECLARED_CLASS_INSTANCE) return nullptr;
    if (!is_tsc_reference(me, declared, 0)) return nullptr;
    Symbol const* decl = declared->declared.symbol;
    if (!decl || declares_members(decl)) return nullptr;
    LowerCtx ctx = { .generic = declared->declared.type_params.len > 0 || declared->declared.this_param };
    Ty const* base = nullptr;
    u32 count = 0;
    for (SymbolDecl const* node = decl->decls; node < decl->decls + decl->decl_count; node++) {
        if (node->kind == SYMBOL_DECL_CLASS) {
            ClassDecl const* class_decl = node->node;
            // (Only a base named by a plain (dotted) name: another expression may lead back to the class)
            Expr const* extends = &class_decl->extends;
            if (extends->tag == EXPR_GENERIC_PARAMS) extends = &extends->generic_params->expr;
            if (extends->tag == EXPR_IMPLICIT_ANY) continue;
            if (extends->tag != EXPR_IDENTIFIER && extends->tag != EXPR_PROPERTY) return nullptr;
            Ty const* instance = nullptr;
            Symbol const* base_decl = nullptr;
            if (class_base(me, class_decl, ctx, &instance, &base_decl) && instance != declared) {
                base = instance;
                count++;
            }
        } else if (node->kind == SYMBOL_DECL_INTERFACE) {
            InterfaceDecl const* interface_decl = node->node;
            for_each (extends, vec_spanned_type, &interface_decl->extends) {
                Ty const* type = ty_from_node(me, &extends->type, ctx);
                if (type == declared || !(type->flags & (TYF_OBJECT | TYF_INTERSECTION))) continue;
                base = type;
                count++;
            }
        }
    }
    if (count != 1) return nullptr;
    if (ty->kind == TY_REFERENCE && declared->declared.type_params.len) {
        TyList args = ty_reference_args(me, ty);
        TyList params = declared->declared.type_params;
        if (args.len >= params.len) {
            base = ty_instantiate(me, base, ty_mapper_new(me, params, (TyList){ .data = args.data, .len = params.len }));
        }
    }
    return base;
}

bool ty_has_base_type(Checker* me, Ty const* source, Ty const* target) {
    if (target->kind == TY_REFERENCE) target = target->reference.target;
    return has_base_type(me, source, target);
}


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

bool ty_bases_reentered(Checker* me, Symbol const* symbol) {
    Ty const* declared = ty_declared_type_of(me, symbol);
    if (declared->kind != TY_DECLARED) return false;
    ty_members(me, declared);
    return ty_symbol_links(me, symbol)->base_cycle & BASE_CYCLE_REENTERED;
}

bool ty_class_base_constructor_circular(Checker* me, ClassDecl const* class_decl) {
    if (!class_decl->symbol) return false;
    Ty const* declared = ty_declared_type_of(me, class_decl->symbol);
    if (declared->kind == TY_DECLARED) ty_members(me, declared);
    return ty_symbol_links(me, class_decl->symbol)->base_cycle & BASE_CYCLE_CONSTRUCTOR;
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

// Whether TS2310 goes on this declaration of an interface in a base cycle. tsc's resolveBaseTypesOfInterface reports a
// base that is the type itself at the declaration naming it (`interface X extends X`, also through an augmentation
// merged with X); only a cycle through other types re-enters getBaseTypes, whose failed popTypeResolution reports at
// every declaration
bool ty_interface_decl_reports_base_cycle(Checker* me, Symbol const* symbol, InterfaceDecl const* decl) {
    Ty const* declared = ty_declared_type_of(me, symbol);
    LowerCtx ctx = { .generic = declared->kind == TY_DECLARED && declared->declared.type_params.len > 0 };
    bool names_itself = false;
    for (SymbolDecl const* node = symbol->decls; node < symbol->decls + symbol->decl_count; node++) {
        if (node->kind != SYMBOL_DECL_INTERFACE) continue;
        InterfaceDecl const* interface_decl = node->node;
        for_each (extends, vec_spanned_type, &interface_decl->extends) {
            Symbol const* base = interface_base_symbol(me, &extends->type, ctx);
            if (base == symbol) {
                names_itself |= interface_decl == decl;
                continue;
            }
            if (!base) continue;
            BaseWalk* walk = malloc(sizeof(BaseWalk));
            panic_if(!walk, "out of memory");
            walk->target = symbol;
            walk->visited_count = 1;
            walk->visited[0] = symbol;
            bool reaches = interface_reaches(me, base, walk);
            free(walk);
            if (reaches) return true;
        }
    }
    return names_itself;
}

// An extends expression (not a class name) whose type has no construct signatures (TS2507) or none that take the
// written type arguments (TS2508): errorType, no base at all (tsc's getBaseConstructorTypeOfClass,
// resolveBaseTypesOfClass)
bool ty_class_base_constructor_unfit(Checker* me, ClassDecl const* class_decl) {
    Expr const* extends = &class_decl->extends;
    u32 count = 0;
    if (extends->tag == EXPR_GENERIC_PARAMS) {
        count = (u32)vec_spanned_type_size(&extends->generic_params->params);
        extends = &extends->generic_params->expr;
    }
    if (extends->tag == EXPR_IMPLICIT_ANY) return false;
    Ty const* type = ty_expr_type(me, extends);
    Ty const* constructor = ty_apparent_type(me, type);
    // (No construct signatures either; `extends null` has no base type, tsc's resolveBaseTypesOfClass)
    if (constructor->flags & (TYF_NEVER | TYF_NULL)) return true;
    // (A type variable is a constructor type only through a constraint with construct signatures: one that is any or
    // the error type isn't, tsc's isConstructorType -> isMixinConstructorType)
    if (((type->flags & TYF_TYPE_PARAM) || type->kind == TY_INDEXED) && !(constructor->flags & (TYF_OBJECT | TYF_INTERSECTION))) {
        return true;
    }
    // (Anything else but any isn't an object type: a primitive like `void` is TS2507 (errorType), a union of
    // constructors no base either, tsc's resolveBaseTypesOfClass)
    if (!(constructor->flags & (TYF_OBJECT | TYF_INTERSECTION))) return !(constructor->flags & TYF_ANY) || (constructor->flags & TYF_ERROR);
    SourceFile const* file = span_source(extends->inner_span);
    // (In JavaScript a class reference's wrong arity is reported and the reference made anyway: tsgo's
    // getTypeFromClassOrInterfaceReference only returns errorType for TypeScript)
    if (file && file->is_js && constructor->kind == TY_DECLARED && constructor->declared.kind == TY_DECLARED_CLASS_STATIC) {
        return !ty_members(me, constructor)->construct_count;
    }
    return !ty_members(me, constructor)->construct_count || !ty_base_constructors_fit(me, constructor, count, file && file->is_js);
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
        if (!class_decl_base_arity_fits(class_decl)) continue; // (errorType: no base at all, not an unknown one)
        Ty const* base = nullptr;
        Symbol const* base_decl = nullptr;
        if (!class_base(me, class_decl, ctx, &base, &base_decl)) {
            // (Only a base that constructs a real any: an unresolved name or import is the error type, a base without
            // members, tsc's isErrorType in getBaseConstructorTypeOfClass)
            Expr const* extends = &class_decl->extends;
            if (extends->tag == EXPR_GENERIC_PARAMS) extends = &extends->generic_params->expr;
            if (ty_expr_type(me, extends)->flags & TYF_ERROR) continue;
            // (A constructor without construct signatures, TS2507, or none that take the written type arguments,
            // TS2508, is errorType, and one that constructs something that can't be a base, TS2509, no base: tsc's
            // getBaseConstructorTypeOfClass, resolveBaseTypesOfClass). A constructor of the error type (`new () =>
            // Unresolved.T`) is no base either (resolveBaseTypesOfClass's isErrorType(baseType))
            if ((base ? !(base->flags & TYF_ANY) || (base->flags & TYF_ERROR) : ty_class_base_constructor_unfit(me, class_decl))) {
                continue;
            }
            return true;
        }
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

// tsc's checkClassLikeDeclaration for a base that isn't a class: the construct signatures that take the written type
// arguments must all return the base instance type (else TS2510). JavaScript constructor functions don't count
bool ty_class_base_constructors_differ(Checker* me, ClassDecl const* class_decl, Ty const* base) {
    Expr const* extends = &class_decl->extends;
    VecSpannedType const* written_args = nullptr;
    if (extends->tag == EXPR_GENERIC_PARAMS) {
        written_args = &extends->generic_params->params;
        extends = &extends->generic_params->expr;
    }
    if (extends->tag == EXPR_IMPLICIT_ANY) return false;
    Ty const* constructor = ty_apparent_type(me, ty_expr_type(me, extends));
    if (!(constructor->flags & (TYF_OBJECT | TYF_INTERSECTION))) return false;
    if (constructor->kind == TY_DECLARED && constructor->declared.symbol && (constructor->declared.symbol->flags & SYMBOL_CLASS)) return false;
    Members const* members = ty_members(me, constructor);
    if (members->construct_count < 2) return false;
    Ty const* declared = class_decl->symbol ? ty_declared_type_of(me, class_decl->symbol) : nullptr;
    LowerCtx ctx = { .generic = declared && declared->kind == TY_DECLARED &&
        (declared->declared.type_params.len || declared->declared.this_param) };
    for (u32 i = 0; i < members->construct_count; i++) {
        Signature const* sig = members->construct[i];
        TyMapper const* mapper;
        if (!base_signature_type_args(me, sig, written_args, ctx, &mapper)) continue;
        Signature const* root = sig;
        while (root && !root->function && root->target) root = root->target;
        if (root && root->function && span_source(root->function->full_span) && span_source(root->function->full_span)->is_js) continue;
        Ty const* constructed = signature_return_type(me, sig);
        if (mapper) constructed = ty_instantiate(me, constructed, mapper);
        if (!ty_is_related(me, constructed, base, TY_RELATION_IDENTITY)) return true;
    }
    return false;
}

// The base a class's `extends` expression constructs when it is an intersection that is never (TS2509, tsc's
// resolveBaseTypesOfClass with elaborateNeverIntersection). Null otherwise
Ty const* ty_class_never_base(Checker* me, ClassDecl const* class_decl) {
    Ty const* base = nullptr;
    Symbol const* base_decl = nullptr;
    Ty const* declared = class_decl->symbol ? ty_declared_type_of(me, class_decl->symbol) : nullptr;
    LowerCtx ctx = { .generic = declared && declared->kind == TY_DECLARED &&
        (declared->declared.type_params.len || declared->declared.this_param) };
    if (class_base(me, class_decl, ctx, &base, &base_decl) || !base) return nullptr;
    return base->kind == TY_INTERSECTION && (ty_reduced(me, base)->flags & TYF_NEVER) ? base : nullptr;
}

Atom const* ty_member_name(Checker* me, cstr const* name_string, Expr const* computed_key) {
    if (!computed_key) return checker_atom(me, cstr_sv(name_string));
    bool is_number = false;
    return ty_property_name_of_key(me, ty_expr_type(me, computed_key), &is_number);
}

static Members const* resolve_tuple_members(Checker* me, Ty const* tuple);
static Members const* declared_raw_members(Checker* me, Ty const* declared);

// Whether the members a base contributes depend on type arguments, so can't be resolved once and instantiated: a type
// parameter, an intersection with one, or a class or interface with such a base (tsc resolves the bases of every
// instantiation, resolveObjectTypeMembers)
static bool base_is_dynamic(Checker* me, Ty const* base, u32 depth) {
    if (depth > 16) return false;
    if (base->kind == TY_TYPE_PARAM) return true;
    if (base->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < base->composite.types.len; i++) {
            if (base_is_dynamic(me, base->composite.types.data[i], depth + 1)) return true;
        }
        return false;
    }
    Ty const* target = base->kind == TY_REFERENCE ? base->reference.target : base;
    if (target->kind != TY_DECLARED || !target->declared.raw) return false;
    if (base->kind == TY_REFERENCE && base->reference.deferred) return false;
    return members_rare(declared_raw_members(me, target))->base_count > 0;
}

// Raw members with their bases' (Members.bases) added as they are
static Members const* complete_members(Checker* me, Members const* raw) {
    if (!members_rare(raw)->base_count) return raw;
    MembersBuilder builder = {0};
    for (u32 i = 0; i < raw->prop_count; i++) builder_push(&builder, props, raw->props[i]);
    for (u32 i = 0; i < raw->call_count; i++) builder_push(&builder, call, raw->call[i]);
    for (u32 i = 0; i < raw->construct_count; i++) builder_push(&builder, construct, raw->construct[i]);
    for (u32 i = 0; i < raw->index_count; i++) builder_push(&builder, index, raw->index[i]);
    for (u32 i = 0; i < members_rare(raw)->base_count; i++) builder_add_inherited(me, &builder, members_rare(raw)->bases[i]);
    // (tsgo's getNamedMembers: the members declared in the type first, then the inherited ones, all of them by where
    // they're declared rather than base by base: ElementContentEditable's before HTMLElement's own in HTMLSpanElement)
    builder_sort_stable_from(&builder, raw->prop_count);
    return builder_finish(me, &builder);
}

// See LowerCtx.merged_owner: for a generic class or interface declared more than once, its index + 1 (0 otherwise)
uint16_t ty_merged_owner_index(Checker* me, Ty const* declared) {
    if (declared->kind != TY_DECLARED || (!declared->declared.type_params.len && !declared->declared.this_param)) return 0;
    Symbol const* decl = declared->declared.symbol;
    u32 count = 0;
    for (u32 i = 0; decl && i < decl->decl_count; i++) {
        count += decl->decls[i].kind == SYMBOL_DECL_INTERFACE || decl->decls[i].kind == SYMBOL_DECL_CLASS;
    }
    if (count < 2) return 0;
    for (u32 i = 0; i < my merged_owners.len; i++) {
        if (my merged_owners.data[i] == declared) return (uint16_t)(i + 1);
    }
    if (my merged_owners.len >= UINT16_MAX) return 0;
    ty_buf_push(&my merged_owners, declared);
    return (uint16_t)my merged_owners.len;
}

// The members as declared: own members, then what the bases add. For a type with a `this` type these mention it, and
// the bases are taken with the this-argument being it (tsc's resolveDeclaredMembers + the base part of
// resolveObjectTypeMembers)
// A class or interface whose members have late-bound names (`[rI.x]: T`): while their keys are evaluated, the type
// already has the members it names syntactically (tsc's getResolvedMembersOrExportsOfSymbol publishes the early-bound
// symbols before late binding), so `[rI.x]` with `rI: RI<"a">` finds `x`. Published on the declared type's states in
// flight, replaced by the full members when they are done. Made only when something asks for the members meanwhile
// (ty_members() and declared_raw_members())
static void early_declared_members(Checker* me, Ty const* ty) {
    if (ty->kind != TY_DECLARED || (ty->declared.kind != TY_DECLARED_INTERFACE && ty->declared.kind != TY_DECLARED_CLASS_INSTANCE) ||
        ty->declared.merged_value || !ty->declared.symbol) {
        return;
    }
    TyObjectState* states[2] = { ty->declared.raw, object_state(ty) };
    bool wanted = false;
    for (u32 k = 0; k < 2; k++) wanted |= states[k] && states[k]->members_state == LAZY_STATE_IN_FLIGHT && !states[k]->members;
    if (!wanted) return;
    // While its bases are lowered: its own members, and the bases are circular (tsc's resolveObjectTypeMembers with
    // getBaseTypes in flight; getBaseTypes resets the type's members once its bases are known)
    for (BasesFrame const* frame = my bases_frames; frame; frame = frame->outer) {
        if (frame->ty != ty) continue;
        Members const* members = builder_snapshot(me, frame->builder);
        for (u32 k = 0; k < 2; k++) {
            if (states[k] && states[k]->members_state == LAZY_STATE_IN_FLIGHT && !states[k]->members) states[k]->members = members;
        }
        ty_resolving_cycle_across_calls(me, RESOLVING_BASES, ty);
        return;
    }
    Symbol const* decl = ty->declared.symbol;
    LowerCtx ctx = { .generic = ty->declared.type_params.len > 0 || ty->declared.this_param };
    if (ctx.generic) ctx.merged_owner = ty_merged_owner_index(me, ty);
    bool late = false;
    for (SymbolDecl const* node = decl->decls; node < decl->decls + decl->decl_count && !late; node++) {
        if (node->kind == SYMBOL_DECL_INTERFACE) {
            for_each (i, vec_interface_member, &((InterfaceDecl const*)node->node)->type.interface.members) {
                late |= i->key.tag == INTERFACE_KEY_EXPR && i->key.expr && expr_is_entity_name(i->key.expr);
            }
        } else if (node->kind == SYMBOL_DECL_CLASS) {
            for_each (i, vec_class_item, &((ClassDecl const*)node->node)->items) {
                Expr const* key = i->tag == CLASS_ITEM_FIELD && !i->field.is_static ? i->field.computed_key :
                    i->tag == CLASS_ITEM_METHOD && !i->method.is_static ? i->method.computed_key : nullptr;
                late |= key && expr_is_entity_name(key);
            }
        }
    }
    if (!late) return;
    MembersBuilder early = { .skip_computed = true };
    for (SymbolDecl const* node = decl->decls; node < decl->decls + decl->decl_count; node++) {
        if (node->kind == SYMBOL_DECL_INTERFACE) {
            builder_add_interface(me, &early, &((InterfaceDecl const*)node->node)->type.interface, ctx, decl);
        } else if (node->kind == SYMBOL_DECL_CLASS) {
            builder_add_class_items(me, &early, node->node, false, ctx, decl);
        }
    }
    Members const* members = builder_finish(me, &early);
    for (u32 k = 0; k < 2; k++) {
        if (states[k] && states[k]->members_state == LAZY_STATE_IN_FLIGHT && !states[k]->members) states[k]->members = members;
    }
}

// Whether instantiating a written base resolves something about the type arguments, as tsc's instantiateType does
// right away: a conditional type (getConditionalTypeInstantiation), an indexed access, a `keyof`, through unions,
// intersections, references' and aliases' type arguments (deferred references are instantiated lazily)
static bool base_resolves_eagerly(Ty const* ty, u32 depth) {
    if (depth > 8 || !(ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return false;
    switch (ty->kind) {
        case TY_CONDITIONAL:
        case TY_INDEXED:
        case TY_KEYOF:
            return true;
        case TY_UNION:
        case TY_INTERSECTION:
            for (u32 i = 0; i < ty->composite.types.len; i++) {
                if (base_resolves_eagerly(ty->composite.types.data[i], depth + 1)) return true;
            }
            break;
        case TY_REFERENCE:
            if (ty->reference.deferred) return false;
            for (u32 i = 0; i < ty->reference.args.len; i++) {
                if (base_resolves_eagerly(ty->reference.args.data[i], depth + 1)) return true;
            }
            return false;
        case TY_ANONYMOUS:
            // An instantiation (`Omit<T, K>`'s Pick): the outer type parameters it mentions are instantiated with it
            // (tsc's getObjectTypeInstantiation)
            if (ty->anonymous.target && ty->anonymous.mapper) {
                for (u32 i = 0; i < ty->anonymous.mapper->len; i++) {
                    if (base_resolves_eagerly(ty->anonymous.mapper->targets[i], depth + 1)) return true;
                }
            }
            break;
        default: break;
    }
    for (u32 i = 0; i < ty->alias_args.len; i++) {
        if (base_resolves_eagerly(ty->alias_args.data[i], depth + 1)) return true;
    }
    return false;
}

// Whether `ty` is or is made of `declared` (through references' arguments, unions, intersections, aliases' arguments)
static bool mentions_declared(Checker* me, Ty const* ty, Ty const* declared, u32 depth) {
    if (depth > 8) return false;
    switch (ty->kind) {
        // (Not as a direct argument, `interface BaseIssue<T> extends Config<BaseIssue<T>>` (valibot): such types are
        // common, and probing them all cost valibot 0.6%. HACK: zod's ZodJSONSchema is nested deeper)
        case TY_DECLARED: return ty == declared && depth >= 2;
        case TY_REFERENCE:
            if (ty->reference.target == declared && depth >= 2) return true;
            if (!ty->reference.deferred || ty->reference.deferred->state == LAZY_STATE_DONE) {
                TyList args = ty_reference_args(me, ty);
                for (u32 i = 0; i < args.len; i++) {
                    if (mentions_declared(me, args.data[i], declared, depth + 1)) return true;
                }
            }
            break;
        case TY_UNION:
        case TY_INTERSECTION:
            for (u32 i = 0; i < ty->composite.types.len; i++) {
                if (mentions_declared(me, ty->composite.types.data[i], declared, depth + 1)) return true;
            }
            break;
        default: break;
    }
    for (u32 i = 0; i < ty->alias_args.len; i++) {
        if (mentions_declared(me, ty->alias_args.data[i], declared, depth + 1)) return true;
    }
    return false;
}

static void set_eager_bases(Checker* me, Members* members, TyBuf* eager) {
    if (eager->len) {
        Ty const** copy = arena_new_n(&my arena, Ty const*, eager->len);
        memcpy(copy, eager->data, sizeof(Ty const*) * eager->len);
        MembersRare* rare = members_rare_mut(me, members);
        rare->eager_bases = copy;
        rare->eager_base_count = eager->len;
    }
    ty_buf_drop(eager);
}

static Members const* resolve_declared_members(Checker* me, Ty const* ty) {
    Symbol const* decl = ty->declared.symbol;
    Ty const* this_param = ty->declared.this_param;
    LowerCtx ctx = { .generic = ty->declared.type_params.len > 0 || this_param };
    if (ctx.generic) ctx.merged_owner = ty_merged_owner_index(me, ty);
    MembersBuilder builder = {0};
    if (ty->declared.merged_value) {
        builder_add_merged_value(me, &builder, ty, ty->declared.merged_value);
        builder_sort_stable(&builder);
        return builder_finish(me, &builder);
    }
    switch (ty->declared.kind) {
        case TY_DECLARED_INTERFACE:
        case TY_DECLARED_CLASS_INSTANCE: {
            // Own members of every merged declaration first, then what their bases add. The index signatures of
            // interfaces after the properties, which are visible meanwhile: tsc's resolveDeclaredMembers sets
            // declaredProperties before it lowers the index infos, so a `keyof C` in C's index signature sees them
            bool index_signatures = false;
            for (SymbolDecl const* node = decl->decls; node < decl->decls + decl->decl_count && !index_signatures; node++) {
                if (node->kind != SYMBOL_DECL_INTERFACE) continue;
                for_each (i, vec_interface_member, &((InterfaceDecl const*)node->node)->type.interface.members) {
                    index_signatures |= i->key.tag == INTERFACE_KEY_INDEX;
                }
            }
            builder.index_pass = index_signatures ? 1 : 0;
            // The early-bound members of every declaration, then the late-bound ones (builder_add_declarations())
            for (u32 pass = 0; pass < 2; pass++) {
                builder.skip_computed = pass == 0;
                builder.only_late = pass == 1;
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
            }
            builder.skip_computed = builder.only_late = false;
            if (index_signatures) {
                TyObjectState* state = object_state(ty);
                if (state && state->members_state == LAZY_STATE_IN_FLIGHT) {
                    Members* partial = arena_new(&my arena, Members);
                    Prop** props = arena_new_n(&my arena, Prop*, builder.props_count ? builder.props_count : 1);
                    memcpy(props, builder.props, sizeof(Prop*) * builder.props_count);
                    partial->props = props;
                    partial->prop_count = builder.props_count;
                    state->members = partial;
                }
                builder.index_pass = 2;
                for (SymbolDecl const* node = decl->decls; node < decl->decls + decl->decl_count; node++) {
                    if (node->kind != SYMBOL_DECL_INTERFACE) continue;
                    builder_add_interface(me, &builder, &((InterfaceDecl const*)node->node)->type.interface, ctx, decl);
                }
                builder.index_pass = 0;
            }
            builder_late_bound_last(&builder, 0);
            builder_add_assignment_members(me, &builder, decl);
            TyBuf bases = {0};
            TyBuf written_bases = {0}; // (Before the this-argument: bases[i] is written_bases[i] if it takes none)
            bool dynamic = false;
            // (tsc's getBaseTypes pushing ResolvedBaseTypes: members asked for while the bases are lowered, see
            // members_reentered())
            ty_resolving_push(me, RESOLVING_BASES, ty);
            BasesFrame frame = { .ty = ty, .builder = &builder, .outer = my bases_frames };
            my bases_frames = &frame;
            bool any_base = false;
            for (SymbolDecl const* node = decl->decls; node < decl->decls + decl->decl_count; node++) {
                if (node->kind == SYMBOL_DECL_CLASS) {
                    Ty const* base = nullptr;
                    Symbol const* base_decl = nullptr;
                    if (class_base(me, node->node, ctx, &base, &base_decl) && base != ty) {
                        ty_buf_push(&bases, this_param ? ty_with_this_arg(me, base, this_param) : base);
                        ty_buf_push(&written_bases, base);
                        dynamic |= base_is_dynamic(me, base, 0);
                    }
                } else if (node->kind == SYMBOL_DECL_INTERFACE) {
                    InterfaceDecl const* interface_decl = node->node;
                    for_each (extends, vec_spanned_type, &interface_decl->extends) {
                        // (Reduced: tsc's resolveBaseTypesOfInterface, getReducedType, which can find a cycle)
                        Ty const* base = ty_reduced(me, ty_from_node(me, &extends->type, ctx));
                        // An object type, a type parameter constrained to one or an intersection of them (tsc's
                        // isValidBaseType)
                        if (base == ty) continue;
                        if (base->flags & (TYF_OBJECT | TYF_INTERSECTION)) {
                            ty_buf_push(&bases, this_param ? ty_with_this_arg(me, base, this_param) : base);
                            ty_buf_push(&written_bases, base);
                            dynamic |= base_is_dynamic(me, base, 0);
                        } else if (base->kind == TY_TYPE_PARAM && is_valid_base_type(me, base, 0)) {
                            ty_buf_push(&bases, base);
                            ty_buf_push(&written_bases, base);
                            dynamic = true;
                        } else if ((base->flags & TYF_ANY) && !(base->flags & TYF_ERROR)) {
                            // An any base gives a string index signature of any (tsc's anyBaseTypeIndexInfo)
                            any_base = true;
                        }
                    }
                }
            }
            if (any_base) {
                Ty const* string = ty_intrinsic(me, TY_STRING);
                bool has_string = false;
                for (u32 i = 0; i < builder.index_count; i++) has_string |= builder.index[i].key == string;
                if (!has_string) {
                    builder_push(&builder, index, ((IndexInfo){ .key = string, .value = ty_intrinsic(me, TY_ANY),
                        .param_name = checker_atom(me, c_sv("x")) }));
                }
            }
            my bases_frames = frame.outer;
            if (ty_resolving_pop(me)) {
                // Its members were asked for while its bases were lowered: TS2310 at its declarations (tsc's
                // getBaseTypes when popTypeResolution fails). The bases that lead back to the type are dropped (tsc's
                // hasBaseType in resolveBaseTypesOfClass/Interface), the others kept: a default type argument reading
                // the class's own members (`class Foo extends Next<Foo>`, `Next<C, T = C['p']>`) still has its base
                SymbolLinks* links = ty_symbol_links(me, decl);
                u32 kept = 0;
                dynamic = false;
                for (u32 i = 0; i < bases.len; i++) {
                    if (has_base_type(me, written_bases.data[i], ty)) continue;
                    dynamic |= base_is_dynamic(me, written_bases.data[i], 0);
                    bases.data[kept] = bases.data[i];
                    written_bases.data[kept++] = written_bases.data[i];
                }
                // (Bases that lead back are a cycle tsc finds elsewhere: TS2506 in getBaseConstructorTypeOfClass, or
                // the interfaces' own TS2310, see ty_interface_in_base_cycle())
                if ((links->base_cycle & BASE_CYCLE_CONSTRUCTOR) && !bases.len) links->base_cycle |= BASE_CYCLE_REENTERED;
                else if (kept < bases.len || !bases.len) links->base_cycle |= BASE_CYCLE_KNOWN | BASE_CYCLE_IN_CYCLE;
                else links->base_cycle |= BASE_CYCLE_REENTERED;
                bases.len = kept;
                written_bases.len = kept;
            }
            bool recursive_bases = false;
            for (u32 i = 0; i < written_bases.len && !recursive_bases; i++) {
                recursive_bases = mentions_declared(me, written_bases.data[i], ty, 0);
            }
            TyBuf eager = {0};
            if (ty->declared.type_params.len) {
                for (u32 i = 0; i < written_bases.len; i++) {
                    if (base_resolves_eagerly(written_bases.data[i], 0)) ty_buf_push(&eager, bases.data[i]);
                }
            }
            if (dynamic) {
                // Members that depend on the type arguments: whoever instantiates these adds the bases' (see
                // Members.bases)
                Members* ret = (Members*)builder_finish(me, &builder);
                Ty const** copy = arena_new_n(&my arena, Ty const*, bases.len);
                memcpy(copy, bases.data, sizeof(Ty const*) * bases.len);
                MembersRare* rare = members_rare_mut(me, ret);
                rare->bases = copy;
                rare->base_count = bases.len;
                set_eager_bases(me, ret, &eager);
                if (recursive_bases) members_rare_mut(me, ret)->recursive_bases = true;
                ty_buf_drop(&bases);
                ty_buf_drop(&written_bases);
                return ret;
            }
            // The signatures of a base that takes no this-argument (a type literal, `interface I extends F<T>` with an
            // alias of a function type) can't mention this type's `this`: a reference that only gives it one shares them
            // rather than instantiating them (tsc concatenates getSignaturesOfType of such a base unchanged, so
            // relating I to its base finds the very same signature)
            TyBuf thisless = {0};
            u32 own_props = builder.props_count;
            for (u32 i = 0; i < bases.len; i++) {
                u32 calls = builder.call_count, constructs = builder.construct_count;
                builder_add_inherited(me, &builder, bases.data[i]);
                if (bases.data[i] == written_bases.data[i]) {
                    for (u32 j = calls; j < builder.call_count; j++) ty_buf_push(&thisless, (Ty const*)(void const*)builder.call[j]);
                    for (u32 j = constructs; j < builder.construct_count; j++) ty_buf_push(&thisless, (Ty const*)(void const*)builder.construct[j]);
                }
            }
            builder_sort_stable_from(&builder, own_props); // (Inherited ones by declaration, see complete_members())
            ty_buf_drop(&bases);
            ty_buf_drop(&written_bases);
            Members const* ret = builder_finish(me, &builder);
            set_eager_bases(me, (Members*)ret, &eager);
            if (recursive_bases) members_rare_mut(me, (Members*)ret)->recursive_bases = true;
            if (thisless.len) {
                Signature const** list = arena_new_n(&my arena, Signature const*, thisless.len + 1);
                memcpy(list, thisless.data, sizeof(Signature const*) * thisless.len);
                ThislessSignatures* entry = arena_new(&my arena, ThislessSignatures);
                *entry = (ThislessSignatures){ .sigs = list, .len = thisless.len };
                *intern_slot(me, &my links, (u64[]){ KEY_THISLESS_SIGNATURES, (u64)(uintptr_t)ret }, 2) = entry;
            }
            ty_buf_drop(&thisless);
            return ret;
        }   break;

        case TY_DECLARED_CLASS_STATIC:
            for (SymbolDecl const* node = decl->decls; node < decl->decls + decl->decl_count; node++) {
                if (node->kind != SYMBOL_DECL_CLASS) continue;
                early_static_members(me, ty, node->node);
                builder_add_class_static_side(me, &builder, ty, node->node);
                builder_late_bound_last(&builder, 0);
                // `var C = class {}; C.x = 1`
                Symbol const* host = ((ClassDecl const*)node->node)->expando_host;
                if (host && host != decl) {
                    Members const* expandos = ty_members(me, ty_namespace_type(me, host));
                    for (u32 i = 0; i < expandos->prop_count; i++) {
                        if (!builder_find_prop(&builder, expandos->props[i]->name)) builder_push(&builder, props, expandos->props[i]);
                    }
                }
                builder_prototype_last(&builder);
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
                // (tsc's globalThisSymbol is readonly: `x.globalThis = ...` is TS2540 through any type that has it)
                if (prop && exports[i].symbol->is_global_this) prop->flags |= PROP_READONLY;
                if (prop) builder_push(&builder, props, prop);
            }
            free(exports);
            // (and tsc's undefinedSymbol, which its globals have: `globalThis.undefined`, `keyof typeof globalThis`)
            if (ty->declared.namespace_symbol->is_global_this) {
                Atom const* name = checker_atom(me, c_sv("undefined"));
                if (!builder_find_prop(&builder, name)) {
                    Prop* prop = arena_new(&my arena, Prop);
                    *prop = (Prop){ .name = name, .type = ty_intrinsic(me, TY_UNDEFINED_WIDENING),
                        .type_state = LAZY_STATE_DONE, .decl = prop_decl_none() };
                    builder_push(&builder, props, prop);
                }
            }
            Symbol const* synthetic_default = symbol_rare(ty->declared.namespace_symbol)->synthetic_default;
            if (synthetic_default) {
                SymbolEntry entry = symbol_entry_make(c_sv("default"), symbol_name_hash(c_sv("default")), (Symbol*)synthetic_default);
                Prop* prop = namespace_export_prop(me, &entry);
                // (Named through the module in the symbols baseline: `d.default`)
                if (prop) prop->decl->symbol = synthetic_default;
                if (prop) builder_push(&builder, props, prop);
            }
            builder_sort_stable(&builder);
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

// An instantiated mapped type's key parameter turned out circular (see ty_mapped_info_instantiate()): tsc's
// getResolvedBaseConstraint reports TS2313 at its constraint declaration, with where the check started (currentNode)
// as related information unless one is inside the other
static void mapped_key_circular(Checker* me, MappedInfo const* source, MappedInfo* mapped) {
    mapped->constraint = ty_intrinsic(me, TY_ERROR);
    Symbol const* symbol = source->type_param->param.symbol;
    InterfaceMember const* member = symbol ? symbol_find_node(symbol, SYMBOL_DECL_MAPPED_KEY) : nullptr;
    if (!member || !span_valid(member->key.mapped->in_type_span) || my errs->phase != ERROR_PHASE_TYPECHECK) return;
    Span at = member->key.mapped->in_type_span;
    Error* e = error_ctx_error_d(my errs, at, 2313, "Type parameter '", member->key.mapped->type_name,
        "' has a circular constraint.");
    Span current = my current_node.expr ? ty_current_node_span(me) : my current_node.span;
    if (span_valid(current) && !span_contains_span(current, at) && !span_contains_span(at, current)) {
        error_add_related_d(e, ty_current_node_span(me), 2751, "Circularity originates in type at this location.");
    }
}

// `this_only`: the mapper only says what `this` is, so members that can't mention it are shared rather than
// instantiated (tsc's createInstantiatedSymbolTable with mappingThisOnly)
MappedInfo const* ty_mapped_info_instantiate(Checker* me, MappedInfo const* source, TyMapper const* mapper) {
    MappedInfo* mapped = arena_new(&my arena, MappedInfo);
    *mapped = *source;
    // tsc's instantiateMappedType makes a fresh key parameter constrained by the instantiated constraint, whose base
    // constraint (getResolvedBaseConstraint, pushed) is resolved when the constraint is asked for: a cycle through it
    // (`interface Y extends X<Y>` with `type X<T> = { [K in keyof T]: ... }`: `keyof Y` needs Y's bases, in flight)
    // makes it circular, TS2313 at the declared constraint, and the constraint the error type. Only where that's the
    // first time the instantiated constraint is computed: instantiateMappedType itself instantiates it (unpushed, the
    // wildcard check) unless the mapped type is homomorphic and its type variable changes (then it maps over that)
    Ty const* variable = source->modifiers_type && source->constraint->kind == TY_KEYOF ?
        ty_actual_type_variable(me, source->constraint->keyof.operand) : nullptr;
    bool pushed = variable && variable->kind == TY_TYPE_PARAM && ty_instantiate(me, variable, mapper) != variable;
    // (Otherwise a marker: tsc's later first resolution of that base constraint is a guard against the eager-base
    // runaway, see RESOLVING_MAPPED_CONSTRAINT)
    ty_resolving_push(me, pushed ? RESOLVING_BASE_CONSTRAINT : RESOLVING_MAPPED_CONSTRAINT, pushed ? (void const*)mapped : (void const*)source);
    mapped->constraint = ty_instantiate(me, mapped->constraint, mapper);
    if (ty_resolving_pop(me) && pushed) mapped_key_circular(me, source, mapped);
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
            .decl = prop_decl_none(),
        };
        builder_push(&builder, props, prop);
    }
    ThislessSignatures const* thisless = this_only ? intern_get(&my links, (u64[]){ KEY_THISLESS_SIGNATURES, (u64)(uintptr_t)source }, 2) : nullptr;
    for (u32 pass = 0; pass < 2; pass++) {
        u32 count = pass ? source->construct_count : source->call_count;
        Signature* const* sigs = pass ? source->construct : source->call;
        for (u32 i = 0; i < count; i++) {
            // (An instantiation expression drops the signatures its type arguments don't apply to)
            if (applies_type_args && !ty_instantiation_keeps_signature(me, sigs[i], mapper)) continue;
            bool shared = false;
            for (u32 j = 0; thisless && j < thisless->len && !shared; j++) shared = thisless->sigs[j] == sigs[i];
            if (shared) {
                if (pass) {
                    builder_push(&builder, construct, sigs[i]);
                } else {
                    builder_push(&builder, call, sigs[i]);
                }
                continue;
            }
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
    if (source->mapped) builder.mapped = ty_mapped_info_instantiate(me, source->mapped, mapper);
    u32 own_props = builder.props_count;
    for (u32 i = 0; i < members_rare(source)->base_count; i++) builder_add_inherited(me, &builder, ty_instantiate(me, members_rare(source)->bases[i], mapper));
    builder_sort_stable_from(&builder, own_props); // (Inherited ones by declaration, see complete_members())
    if (!members_rare(source)->base_count) builder.same_names = source;
    Members* ret = (Members*)builder_finish(me, &builder);
    if (members_rare(source)->expanded_from) {
        members_rare_mut(me, ret)->expanded_from = ty_mapped_info_instantiate(me, members_rare(source)->expanded_from, mapper);
    }
    return ret;
}

static Members const* resolve_instantiated_members(Checker* me, Ty const* target, TyMapper const* mapper, bool applies_type_args) {
    return instantiate_members(me, ty_members(me, target), mapper, applies_type_args, false);
}

// Members of a class or interface asked for while they are resolved: if it's while its bases are, they are circular
static void members_reentered(Checker* me, Ty const* ty) {
    if (ty->kind == TY_REFERENCE) ty = ty->reference.target;
    if (ty->kind == TY_DECLARED) ty_resolving_cycle_across_calls(me, RESOLVING_BASES, ty);
}

static void members_in_flight_push(Checker* me, Ty const* ty) {
    if (my members_in_flight_len == my members_in_flight_cap) {
        my members_in_flight_cap = my members_in_flight_cap ? my members_in_flight_cap * 2 : 64;
        my members_in_flight = realloc(my members_in_flight, sizeof(MembersInFlight) * my members_in_flight_cap);
        panic_if(!my members_in_flight, "out of memory");
    }
    my members_in_flight[my members_in_flight_len++] = (MembersInFlight){
        .ty = ty, .resolving_len = my resolving_len, .instantiation_depth = my instantiation_depth,
    };
}

// tsc's resolveStructuredTypeMembers has no in-flight state: members asked for again while they are being resolved are
// resolved again from the start. Where that happens while a reference's bases are instantiated (probe_eager_bases()),
// the second round instantiates them again with a new mapper (no cache hit) and gets back here, until the instantiation
// stack is 100 deep: TS5114/TS5115 at the current node. Unless the way here passes something tsc does guard
// (pushTypeResolution: a declaration's type, an alias, a base constraint...), which the second round finds in flight:
// tttt's resolving stack, unchanged since the members started. HACK: an approximation, tsc pushes some entries tttt
// doesn't (a mapped type's constraint from isGenericMappedType)
static void members_reentered_unguarded(Checker* me, Ty const* ty) {
    if (my eager_probe_reported) return;
    for (u32 i = my eager_probe_floor; i-- > 0;) {
        MembersInFlight const* entry = &my members_in_flight[i];
        if (entry->ty != ty) continue;
        if (entry->resolving_len != my resolving_len) return;
        my eager_probe_reported = true;
        // The stack tsc has when it stops: the instantiations since the members started, repeated
        u32 depth = my instantiation_depth < c_arraylen(my instantiation_stack) ? my instantiation_depth : (u32)c_arraylen(my instantiation_stack);
        u32 from = entry->instantiation_depth < depth ? entry->instantiation_depth : depth;
        if (from == depth) return; // (No instantiation on the way: no runaway either)
        Ty const* stack[100];
        u32 len = 0;
        for (u32 k = 0; k < from; k++) stack[len++] = my instantiation_stack[k];
        while (len < c_arraylen(stack)) {
            for (u32 k = from; k < depth && len < c_arraylen(stack); k++) stack[len++] = my instantiation_stack[k];
        }
        ty_instantiation_limit_reached_on(me, stack, len);
        return;
    }
}

// tsc's resolveObjectTypeMembers instantiates a reference's bases with its type arguments whenever its members are
// resolved (getPropertiesOfType of each instantiated base, recursively), where tttt instantiates the raw members, which
// already have the bases'. Only the bases whose instantiation resolves something (MembersRare.eager_bases) can make a
// difference: instantiated here for that, the results dropped. Not while another reference's are (only along the
// chain of bases): tttt resolves members tsc doesn't (mapped types' keys eagerly), which would chain probes tsc never makes
static void probe_reference(Checker* me, Ty const* ty, u32 depth);
static void probe_eager_bases(Checker* me, Members const* raw, TyMapper const* mapper, u32 depth) {
    MembersRare const* rare = members_rare(raw);
    if (!rare->eager_base_count || depth > 8 || (depth == 0 && my eager_probe_depth)) return;
    if (!my eager_probe_depth) {
        my eager_probe_floor = my members_in_flight_len;
        my eager_probe_reported = false;
    }
    my eager_probe_depth++;
    for (u32 i = 0; i < rare->eager_base_count; i++) {
        Ty const* base = ty_instantiate(me, rare->eager_bases[i], mapper);
        if (base->kind != TY_REFERENCE || base->reference.deferred) continue;
        Ty const* target = base->reference.target;
        if (target->kind != TY_DECLARED || !target->declared.raw || target->declared.raw->members_state != LAZY_STATE_DONE) continue;
        probe_reference(me, base, depth + 1);
    }
    my eager_probe_depth--;
}

// Whether a type argument is, or is made of, a class, interface or reference whose members are in flight: what an
// eager base's instantiation can get back to (anything else it reaches is the same for every reference)
static bool mentions_members_in_flight(Checker* me, Ty const* ty, u32 depth) {
    if (depth > 8) return false;
    switch (ty->kind) {
        case TY_DECLARED:
        case TY_REFERENCE: {
            for (u32 i = my members_in_flight_len; i-- > 0;) {
                if (my members_in_flight[i].ty == ty) return true;
            }
            Ty const* declared = ty->kind == TY_REFERENCE ? ty->reference.target : ty;
            if (declared->kind == TY_DECLARED && declared->declared.raw && declared->declared.raw->members_state == LAZY_STATE_DONE &&
                members_rare(declared->declared.raw->members)->recursive_bases
            ) {
                return true;
            }
            // (A deferred reference's arguments once they are resolved)
            if (ty->kind == TY_REFERENCE && (!ty->reference.deferred || ty->reference.deferred->state == LAZY_STATE_DONE)) {
                TyList args = ty_reference_args(me, ty);
                for (u32 i = 0; i < args.len; i++) {
                    if (mentions_members_in_flight(me, args.data[i], depth + 1)) return true;
                }
            }
            break;
        }
        case TY_UNION:
        case TY_INTERSECTION:
            for (u32 i = 0; i < ty->composite.types.len; i++) {
                if (mentions_members_in_flight(me, ty->composite.types.data[i], depth + 1)) return true;
            }
            break;
        case TY_INDEXED:
            return mentions_members_in_flight(me, ty->indexed.object, depth + 1) || mentions_members_in_flight(me, ty->indexed.index, depth + 1);
        case TY_KEYOF:
            return mentions_members_in_flight(me, ty->keyof.operand, depth + 1);
        default: break;
    }
    for (u32 i = 0; i < ty->alias_args.len; i++) {
        if (mentions_members_in_flight(me, ty->alias_args.data[i], depth + 1)) return true;
    }
    return false;
}

// probe_eager_bases() for a reference whose members are known (and still in flight: the second round gets them)
static void probe_reference(Checker* me, Ty const* ty, u32 depth) {
    Ty const* target = ty->reference.target;
    if (target->kind != TY_DECLARED || !target->declared.raw || target->declared.raw->members_state != LAZY_STATE_DONE) return;
    Members const* raw = target->declared.raw->members;
    if (!members_rare(raw)->eager_base_count) return;
    // (Where nothing it can reach is in flight, it can't run away: not worth instantiating. Only the outermost reference
    // is looked at, the bases along the chain are instantiated from it)
    if (depth == 0) {
        TyList args = ty_reference_args(me, ty);
        bool mentions = false;
        for (u32 i = 0; i < args.len && !mentions; i++) mentions = mentions_members_in_flight(me, args.data[i], 0);
        if (!mentions) return;
    }
    TyList params = target->declared.type_params;
    TyList args = ty_reference_args(me, ty);
    u32 n = params.len + (target->declared.this_param ? 1 : 0);
    Ty const** sources = arena_new_n(&my arena, Ty const*, n);
    Ty const** targets = arena_new_n(&my arena, Ty const*, n);
    for (u32 k = 0; k < params.len; k++) {
        sources[k] = params.data[k];
        targets[k] = args.data[k];
    }
    if (target->declared.this_param) {
        sources[params.len] = target->declared.this_param;
        targets[params.len] = ty->reference.this_arg ? ty->reference.this_arg : ty;
    }
    probe_eager_bases(me, raw, ty_mapper_new(me, (TyList){ .data = sources, .len = n }, (TyList){ .data = targets, .len = n }), depth);
}

// The raw members of a class or interface (see resolve_declared_members()), memoised in Ty.declared.raw
static Members const* declared_raw_members(Checker* me, Ty const* declared) {
    TyObjectState* state = declared->declared.raw;
    switch (state->members_state) {
        case LAZY_STATE_DONE: return state->members;
        case LAZY_STATE_IN_FLIGHT: // `interface A extends A`
            if (my eager_probe_depth) members_reentered_unguarded(me, declared);
            if (!state->members) early_declared_members(me, declared);
            if (state->members) return state->members; // (The early members, see early_declared_members())
            members_reentered(me, declared);
            return &empty_members;
        case LAZY_STATE_NOT_STARTED: break;
    }
    state->members_state = LAZY_STATE_IN_FLIGHT;
    members_in_flight_push(me, declared);
    state->members = resolve_declared_members(me, declared);
    my members_in_flight_len--;
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

static Members const* expand_generic_intersection_homomorphic(Checker* me, Members const* members, TyList parts);
static Prop const* modifiers_prop(Checker* me, Ty const* modifiers, Atom const* name, u32* flags);

// A reference whose type arguments aren't generic, only its this-argument: `T extends I` with an interface I that has a
// this-type is apparently `I<this = T>`, whose property names are I's
static bool only_this_arg_generic(Checker* me, Ty const* ty) {
    if (ty->kind != TY_REFERENCE || !ty->reference.this_arg || ty_is_array(me, ty) || ty_is_tuple(ty)) return false;
    TyList args = ty_reference_args(me, ty);
    for (u32 i = 0; i < args.len; i++) {
        if (args.data[i]->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) return false;
    }
    return true;
}

// A class or interface instantiated with type variables (not an array or tuple, whose mapped types are arrays and tuples),
// or an object type that isn't a mapped one (`{ a: keyof D & string }`, the constraint of `{ [P in keyof D]: { a: P }
// }[keyof D]`): its property names are known all the same, `T extends Ctx<P>` has Ctx's (tsc's resolveMappedTypeMembers lists
// getPropertiesOfType of the apparent modifiers type whatever its type arguments)
static bool generic_object_reference(Checker* me, Ty const* ty) {
    if (ty->kind == TY_ANONYMOUS) return !ty_members(me, ty)->mapped;
    return ty->kind == TY_REFERENCE && !ty_is_array(me, ty) && !ty_is_tuple(ty);
}

// `{ [K in keyof T]: X }` with T generic stays a mapped type, but has a property per property of T's apparent type (its
// constraint), X with K the property's name, with T's modifiers (tsc's resolveMappedTypeMembers, whose modifiers type
// is the apparent type): `Mutable<T>` with `T extends Node` has `pos: T["pos"]`
static Members const* expand_generic_homomorphic(Checker* me, Members const* members) {
    MappedInfo const* mapped = members->mapped;
    if (!mapped->keyof_constraint || mapped->name_type || !mapped->modifiers_type) return members;
    if (mapped->modifiers_type->kind == TY_INTERSECTION) {
        return expand_generic_intersection_homomorphic(me, members, mapped->modifiers_type->composite.types);
    }
    Ty const* apparent = ty_apparent_type(me, mapped->modifiers_type);
    // (A generic mapped type is its own apparent type, and has the properties of the lower bound of its keys:
    // `Partial<M<T>>` with `M<T> = { [K in keyof T]: T[K] }` has T's constraint's, tsc's resolveMappedTypeMembers over
    // getPropertiesOfType of its modifiers type)
    if (apparent == mapped->modifiers_type && apparent->kind == TY_ANONYMOUS && ty_members(me, apparent)->mapped) {
        return expand_generic_intersection_homomorphic(me, members, (TyList){ .data = &apparent, .len = 1 });
    }
    // (Of a constraint that's an intersection, `T extends Node & { x: X }`, the properties of its parts; of a deferred
    // conditional type's, `Extract<{ a: A }, { t: T }>`'s `{ t: T } & { a: A }` or `A | B`, the properties every
    // member of the union has, whatever their genericity: tsc's getPropertiesOfType of the apparent modifiers type)
    if (apparent != mapped->modifiers_type && apparent->kind == TY_INTERSECTION &&
        (mapped->modifiers_type->flags & TYF_DEFERRED || !(apparent->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES))) {
        return expand_generic_intersection_homomorphic(me, members, apparent->composite.types);
    }
    if (apparent != mapped->modifiers_type && apparent->kind == TY_UNION) {
        return expand_generic_intersection_homomorphic(me, members, (TyList){ .data = &mapped->modifiers_type, .len = 1 });
    }
    // (A generic class or interface reference whose bases resolve has its properties whatever its type arguments,
    // `T extends Ctx<P>` has Ctx's, and none if it has none: `Partial<T>` with `T extends RData<H>` has no `open`)
    if (apparent == mapped->modifiers_type || ((apparent->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) &&
        !only_this_arg_generic(me, apparent) && !(generic_object_reference(me, apparent) && !ty_has_unresolved_base(me, apparent)))) {
        return members;
    }
    if (!(apparent->flags & (TYF_OBJECT | TYF_UNKNOWN))) return members;
    Members const* source = (apparent->flags & TYF_OBJECT) ? ty_members(me, apparent) : nullptr;
    if (source && source->mapped) return members;
    if (!source || (!source->prop_count && !source->index_count)) {
        // (No constraint or one without members, `P extends {}`: none, and that's known, `Readonly<P>` has no `foo`)
        MembersBuilder empty = { .mapped = mapped };
        Members* ret = (Members*)builder_finish(me, &empty);
        members_rare_mut(me, ret)->generic_keys_known = true;
        return ret;
    }
    Prop* template = prop_new(me);
    template->type = ty_mapped_template(me, mapped);
    template->type_state = LAZY_STATE_DONE;
    MembersBuilder builder = { .mapped = mapped };
    for (u32 i = 0; i < source->prop_count; i++) {
        Prop const* from = source->props[i];
        if (!ty_prop_is_key(me, from)) continue;
        Ty const* key = ty_prop_key_literal(me, from);
        TyMapper const* mapper = ty_mapper_new(
            me, (TyList){ .data = &mapped->type_param, .len = 1 }, (TyList){ .data = &key, .len = 1 }
        );
        u32 flags = from->flags & (PROP_OPTIONAL | PROP_READONLY);
        if (mapped->optional > 0) flags |= PROP_OPTIONAL;
        if (mapped->optional < 0 && (flags & PROP_OPTIONAL)) {
            flags &= ~(u32)PROP_OPTIONAL;
            if (compiler_options()->strict_null_checks) flags |= PROP_STRIP_UNDEFINED;
        }
        if (mapped->readonly > 0) flags |= PROP_READONLY;
        if (mapped->readonly < 0) flags &= ~(u32)PROP_READONLY;
        Prop* prop = arena_new(&my arena, Prop);
        // (A mapped symbol: declarations but no valueDeclaration in tsc)
        *prop = (Prop){ .name = from->name, .flags = flags | PROP_NO_VALUE_DECLARATION, .target = template, .mapper = mapper,
            .decl = prop_decl_none() };
        Prop const* declared = from;
        while (declared && declared->target && !declared->decl->decl_span_count) declared = declared->target;
        if (declared) {
            prop->decl = arena_new(&my arena, PropDecl);
            prop->decl->name_span = declared->decl->name_span;
            prop->decl->name_form = ty_prop_name_form_of(declared);
            prop->decl->decl_spans = declared->decl->decl_spans;
            prop->decl->decl_span_count = declared->decl->decl_span_count;
            prop->decl->name_is_value = declared->decl->name_is_value;
        }
        builder_push(&builder, props, prop);
    }
    // And an index signature per index signature of the constraint (tsc's addMemberForKeyTypeWorker)
    for (u32 i = 0; i < source->index_count; i++) {
        IndexInfo const* from = &source->index[i];
        TyMapper const* mapper = ty_mapper_new(
            me, (TyList){ .data = &mapped->type_param, .len = 1 }, (TyList){ .data = &from->key, .len = 1 }
        );
        Ty const* value = ty_mapped_template_instantiated(me, mapped, mapper);
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
// Adds the declarations of `name` in every member of a union or intersection (apparent types, nested ones too) that
// `prop` doesn't have yet: the symbols baseline's union property declarations
static void add_member_decl_spans(Checker* me, Prop* prop, Ty const* ty, Atom const* name, u32 depth) {
    if (depth > 8) return;
    ty = ty_apparent_type(me, ty);
    if (ty->kind == TY_UNION || ty->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) add_member_decl_spans(me, prop, ty->composite.types.data[i], name, depth + 1);
        return;
    }
    Prop const* other = (ty->flags & TYF_OBJECT) ? ty_get_prop(me, ty, name) : nullptr;
    while (other && other->target && !other->decl->decl_span_count) other = other->target;
    if (!other) return;
    for (u32 k = 0; k < other->decl->decl_span_count; k++) {
        bool seen = false;
        for (u32 q = 0; q < prop->decl->decl_span_count && !seen; q++) seen = span_eq(prop->decl->decl_spans[q], other->decl->decl_spans[k]);
        if (!seen) prop_add_decl_spans(me, prop, &other->decl->decl_spans[k], 1);
    }
}

static Members const* expand_generic_intersection_homomorphic(Checker* me, Members const* members, TyList parts) {
    MappedInfo const* mapped = members->mapped;
    Prop* template = nullptr;
    MembersBuilder builder = { .mapped = mapped };
    for (u32 p = 0; p < parts.len; p++) {
        Ty const* part = parts.data[p];
        if (!(part->flags & TYF_OBJECT)) part = ty_apparent_type(me, part);
        // (A part whose apparent type is a union, a deferred conditional type's constraint `{ c?: F } | { c: F }`, has
        // the properties every member has: tsc's getPropertiesOfType of the apparent type. Listed from the first member)
        Ty const* whole = part;
        if (part->kind == TY_UNION && part->composite.types.len) {
            part = ty_apparent_type(me, part->composite.types.data[0]);
        } else {
            whole = nullptr;
        }
        if (!(part->flags & TYF_OBJECT)) continue;
        Members const* source = ty_members(me, part);
        for (u32 i = 0; i < source->prop_count; i++) {
            Prop const* from = source->props[i];
            if (!ty_prop_is_key(me, from) || builder_find_prop(&builder, from->name)) continue;
            // Optional only if every part that has it has it optional, readonly if any has it readonly
            u32 flags = from->flags & (PROP_OPTIONAL | PROP_READONLY);
            if (whole && !modifiers_prop(me, whole, from->name, &flags)) continue;
            for (u32 q = p + 1; q < parts.len; q++) {
                Ty const* other = parts.data[q];
                if (!(other->flags & TYF_OBJECT)) other = ty_apparent_type(me, other);
                u32 also_flags = 0;
                Prop const* also = (other->flags & TYF_OBJECT) || other->kind == TY_UNION ?
                    modifiers_prop(me, other, from->name, &also_flags) : nullptr;
                if (!also) continue;
                if (!(also_flags & PROP_OPTIONAL)) flags &= ~(u32)PROP_OPTIONAL;
                flags |= also_flags & PROP_READONLY;
            }
            if (mapped->optional > 0) flags |= PROP_OPTIONAL;
            if (mapped->optional < 0 && (flags & PROP_OPTIONAL)) {
                flags &= ~(u32)PROP_OPTIONAL;
                if (compiler_options()->strict_null_checks) flags |= PROP_STRIP_UNDEFINED;
            }
            if (mapped->readonly > 0) flags |= PROP_READONLY;
            if (mapped->readonly < 0) flags &= ~(u32)PROP_READONLY;
            if (!template) {
                template = prop_new(me);
                template->type = ty_mapped_template(me, mapped);
                template->type_state = LAZY_STATE_DONE;
            }
            Ty const* key = ty_prop_key_literal(me, from);
            TyMapper const* mapper = ty_mapper_new(
                me, (TyList){ .data = &mapped->type_param, .len = 1 }, (TyList){ .data = &key, .len = 1 }
            );
            Prop* prop = arena_new(&my arena, Prop);
            // (A mapped symbol: declarations but no valueDeclaration in tsc)
            *prop = (Prop){ .name = from->name, .flags = flags | PROP_NO_VALUE_DECLARATION, .target = template, .mapper = mapper,
                .decl = prop_decl_none() };
            Prop const* declared = from;
            while (declared && declared->target && !declared->decl->decl_span_count) declared = declared->target;
            if (declared) {
                prop->decl = arena_new(&my arena, PropDecl);
                prop->decl->name_span = declared->decl->name_span;
                prop->decl->name_form = ty_prop_name_form_of(declared);
                prop->decl->decl_spans = declared->decl->decl_spans;
                prop->decl->decl_span_count = declared->decl->decl_span_count;
                prop->decl->name_is_value = declared->decl->name_is_value;
                // (A union's property is declared by every member's: tsc's createUnionOrIntersectionProperty)
                if (whole) add_member_decl_spans(me, prop, whole, from->name, 0);
            }
            builder_push(&builder, props, prop);
        }
        // A generic homomorphic mapped part's index signatures too, the template at their keys (tsc's
        // resolveMappedTypeMembers, forEachMappedTypePropertyKeyTypeAndIndexSignatureKeyType of the modifiers type):
        // `Nullable<Vals<K>>` with `Vals<K>` mapped over `keyof K`, K's constraint `Record<string, X>`, has a string
        // index signature of `Vals<K>[string] | null`. (Only those: the other parts' would be tsc's too, but inferring
        // from them changes localTypeParameterInferencePriority's candidates, unexplained)
        bool homomorphic_part = source->mapped && source->mapped->keyof_constraint && !source->mapped->name_type;
        for (u32 j = 0; !whole && homomorphic_part && j < source->index_count; j++) {
            IndexInfo const* from = &source->index[j];
            bool seen = false;
            for (u32 k = 0; k < builder.index_count && !seen; k++) seen = builder.index[k].key == from->key;
            if (seen) continue;
            TyMapper const* mapper = ty_mapper_new(
                me, (TyList){ .data = &mapped->type_param, .len = 1 }, (TyList){ .data = &from->key, .len = 1 }
            );
            Ty const* value = ty_mapped_template_instantiated(me, mapped, mapper);
            if (mapped->optional > 0 && compiler_options()->strict_null_checks) {
                Ty const* both[2] = { value, ty_intrinsic(me, TY_UNDEFINED) };
                value = ty_union(me, both, 2, nullptr, (TyList){0});
            }
            IndexInfo info = {
                .key = from->key,
                .value = value,
                .param_name = checker_atom(me, c_sv("x")),
                .readonly = mapped->readonly > 0 || (!mapped->readonly && from->readonly),
            };
            builder_push(&builder, index, info);
        }
    }
    return builder.props_count || builder.index_count ? builder_finish(me, &builder) : members;
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

static bool prop_has_declaration(Prop const* prop) {
    while (prop && prop->target && !prop->decl->decl_span_count) prop = prop->target;
    return prop && prop->decl->decl_span_count;
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
        // (The first one declared somewhere: tsc's union or intersection property has every member's declarations,
        // which the first declared one's lead, and getNamedMembers orders a mapped type's properties by them)
        if (!first || (!prop_has_declaration(first) && prop_has_declaration(prop))) first = prop;
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
        // (A union without reductions, tsc's mapType with noReductions: `keyof T | "k"` with T's keys string keeps "k")
        Ty const* ret = !changed ? ty : ty->kind == TY_UNION ? ty_union_unreduced(me, parts.data, parts.len) :
            ty_intersection(me, parts.data, parts.len, nullptr, (TyList){0});
        ty_buf_drop(&parts);
        return ret;
    }
    return ty;
}

Ty const* ty_lower_bound_of_key_type(Checker* me, Ty const* ty) {
    return lower_bound_of_key_type(me, ty, 0);
}

// An intersection member that is a type variable or a still generic mapped type (tsc's isGenericObjectType)
static bool intersection_has_generic_part(Checker* me, Ty const* intersection) {
    for (u32 i = 0; i < intersection->composite.types.len; i++) {
        Ty const* part = intersection->composite.types.data[i];
        if (part->kind == TY_TYPE_PARAM || (part->flags & TYF_DEFERRED)) return true;
        if (part->kind == TY_ANONYMOUS && (part->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) && ty_members(me, part)->mapped) return true;
        if (!(part->flags & TYF_OBJECT)) return true;
    }
    return false;
}

// A conditional type's branch without lowering or instantiating it (that can reach the mapped type whose members are
// being resolved): one still to be lowered is unknown (null), one still to be instantiated is known if it's a type
// parameter (Exclude's `T`) or never
static Ty const* peek_cond_branch(TyCondBranch const* branch) {
    if ((!branch->node && !branch->source) || branch->state == LAZY_STATE_DONE) return branch->type;
    if (branch->node || branch->state != LAZY_STATE_NOT_STARTED) return nullptr;
    Ty const* source = branch->source;
    if (source->flags & TYF_NEVER) return source;
    if (source->kind != TY_TYPE_PARAM) return nullptr;
    for (u32 i = 0; branch->mapper && i < branch->mapper->len; i++) {
        if (branch->mapper->sources[i] == source) return branch->mapper->targets[i];
    }
    return source;
}

// Whether an `as` clause only filters the keys (tsc's getMappedTypeNameTypeKind: Filtering when the clause is assignable
// to the key parameter), told by its shape: each union member and conditional branch is the key, never, or an
// intersection with the key. (Relating it as tsc does can reach the members of the mapped type being resolved:
// `as `${K}.${keyof Self<T[K]>}``)
static bool name_type_filters(Checker* me, Ty const* name, Ty const* key, u32 depth) {
    if (name == key || (name->flags & TYF_NEVER)) return true;
    if (depth > 8) return false;
    switch ((TyKind)name->kind) {
        case TY_UNION:
            for (u32 i = 0; i < name->composite.types.len; i++) {
                if (!name_type_filters(me, name->composite.types.data[i], key, depth + 1)) return false;
            }
            return true;
        case TY_INTERSECTION:
            for (u32 i = 0; i < name->composite.types.len; i++) {
                if (name_type_filters(me, name->composite.types.data[i], key, depth + 1)) return true;
            }
            return false;
        case TY_CONDITIONAL: {
            for (u32 which = 0; which < 2; which++) {
                TyCondBranch const* cond = &name->conditional.branches[which];
                Ty const* branch = peek_cond_branch(cond);
                // (One still to be lowered, by its syntax: `never`, or the key's own name)
                if (!branch && cond->node && cond->state == LAZY_STATE_NOT_STARTED) {
                    TypeNode const* node = cond->node;
                    if (node->tag == TYPE_NEVER) continue;
                    if (node->tag == TYPE_IDENTIFIER && key->kind == TY_TYPE_PARAM && key->param.name &&
                        !identifier_type_namespace_count(node->identifier) &&
                        !vec_spanned_type_size(&node->identifier->generic_params) &&
                        csview_equals_sv(span_sv(node->identifier->name), atom_sv(key->param.name))
                    ) {
                        continue;
                    }
                }
                if (!branch || !name_type_filters(me, branch, key, depth + 1)) return false;
            }
            return true;
        }
        case TY_SUBSTITUTION:
            return name_type_filters(me, name->substitution.base, key, depth + 1);
        default:
            return false;
    }
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
    Ty const* keyed = mapped->modifiers_type;
    bool over_union = false;
    if (ty_is_generic_index(constraint)) {
        // (Over a T constrained to a union, `T extends A | B`, the properties of the union, which are those of the
        // lower bound of the keys `keyof (A | B)` below, with the union's modifiers)
        Ty const* keyed_apparent = mapped->keyof_constraint && keyed && keyed->kind == TY_TYPE_PARAM ?
            ty_apparent_type(me, keyed) : nullptr;
        over_union = keyed_apparent && keyed_apparent->kind == TY_UNION &&
            !(keyed_apparent->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES);
        // (Not over a union of arrays and tuples, `TItems extends A[] | readonly A[]`: such a mapped type is apparently
        // an array (tsc's getResolvedApparentTypeOfMappedType), and the array methods as properties of the generic
        // mapped type only make relations compare them, which cost valibot 3.6%)
        bool lists = over_union;
        for (u32 i = 0; lists && i < keyed_apparent->composite.types.len; i++) {
            Ty const* part = keyed_apparent->composite.types.data[i];
            lists = ty_is_array(me, part) || ty_tuple_shape(part); // (`[]` too, the tuple target itself)
        }
        if (lists) over_union = false;
        if (mapped->keyof_constraint && !mapped->name_type && !over_union) return expand_generic_homomorphic(me, members);
        if (mapped->keyof_constraint && mapped->name_type) {
            // With an `as` clause, the names of the keys of the constraint of T (`[K in keyof T as `slot-${K & string}`]`
            // with `T extends { foo: X }` has slot-foo, tsc's forEachMappedTypePropertyKeyTypeAndIndexSignatureKeyType
            // over the type parameter's properties)
            if (!keyed || keyed->kind == TY_INTERSECTION) return members;
            Ty const* apparent = ty_apparent_type(me, keyed);
            if (apparent == keyed || !(apparent->flags & TYF_OBJECT) || (apparent->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) {
                return members;
            }
            keyed = apparent;
            generic = true;
        }
    }
    if (ty_is_generic_index(constraint) && !generic) {
        Ty const* lower = ty_lower_bound_of_key_type(me, constraint);
        // (A union's literal members are keys even when its other members stay generic: `"x" | (T extends U ? "y" :
        // never)` has x, tsc's forEachType over the lower bound)
        if (lower == constraint && constraint->kind != TY_UNION) return members;
        constraint = lower;
        generic = true;
    }
    Ty const* const* keys;
    u32 key_count;
    union_members(&constraint, &keys, &key_count);
    // (A union with no common keys, `T extends X | undefined`, adds nothing: the mapped type stays as it was)
    // HACK: nor one with only index signature keys (`TEntries extends ObjectEntries | ObjectEntriesAsync`), which tsc
    // gives an index signature: relations then evaluate its template's conditional types for `T[string]`, whose
    // constraint this checker gets as unknown, costing valibot 3.6% (hacks.md)
    bool literal_key = false;
    for (u32 i = 0; i < key_count && !literal_key; i++) {
        literal_key = (keys[i]->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL | TYF_UNIQUE_ES_SYMBOL)) != 0;
    }
    if (over_union && !literal_key) return members;
    Ty const** member_keys = nullptr;
    // (`NoInfer<X>` goes by X's members: tsc's getPropertiesOfType reads a substitution type's apparent type)
    if (keyed && ty_is_no_infer(keyed)) keyed = keyed->substitution.base;
    // (An intersection of object types that aren't generic: TYF_COULD_CONTAIN_TYPE_VARIABLES says too little, any type
    // literal with a `typeof` in it has it)
    bool keyed_intersection = keyed && keyed->kind == TY_INTERSECTION &&
        (!(keyed->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) || !intersection_has_generic_part(me, keyed));
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
                if (i < source->prop_count && !ty_prop_is_key(me, source->props[i])) continue;
                Ty const* key = i < source->prop_count ? ty_prop_key_literal(me, source->props[i]) :
                    source->index[i - source->prop_count].key;
                bool seen = false;
                for (u32 j = 0; j < key_count && !seen; j++) seen = member_keys[j] == key;
                if (!seen) member_keys[key_count++] = key;
            }
        }
        keys = member_keys;
    } else if (mapped->keyof_constraint && keyed && (keyed->flags & TYF_ANY)) {
        // `[K in keyof any]` goes by any, whose one key is string (tsc's
        // forEachMappedTypePropertyKeyTypeAndIndexSignatureKeyType)
        member_keys = arena_new(&my arena, Ty const*);
        member_keys[0] = ty_intrinsic(me, TY_STRING);
        keys = member_keys;
        key_count = 1;
    }
    if (mapped->name_type && !generic) {
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
    // (Its properties as tsc's getPropertyOfType finds them, on the apparent type: `Pick<T, "a" | "c">` has T's
    // constraint's modifiers)
    Ty const* modifiers = mapped->modifiers_type;
    if (modifiers) modifiers = ty_apparent_type(me, modifiers);
    if (modifiers && !(modifiers->flags & TYF_OBJECT) && modifiers->kind != TY_UNION && modifiers->kind != TY_INTERSECTION) {
        modifiers = nullptr;
    }
    // The template as a property to instantiate per key (lowered when a property's type is first asked for, like tsc's
    // getTypeOfMappedSymbol: the members can be there before the template can be lowered)
    Prop* template = prop_new_declared(me);
    prop_decl_rare_mut(me, template->decl)->template_of = mapped;

    MembersBuilder builder = { .mapped = generic ? mapped : nullptr };
    // (Of the declared mapped type, tsc's type.target)
    MappedInfo const* declared_mapped = mapped;
    while (declared_mapped->template_from) declared_mapped = declared_mapped->template_from;
    bool remapping = declared_mapped->name_type &&
        !name_type_filters(me, declared_mapped->name_type, declared_mapped->type_param, 0);
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
                    // (tsc's nameType becomes the union of both names, which is no member's: the plain literal)
                    Ty const* existing_name = prop_decl_rare(existing->decl)->name_type;
                    if (existing_name && (existing_name->flags & TYF_ENUM_LITERAL) && existing_name != ty_regular(me, prop_name)) {
                        prop_decl_rare_mut(me, existing->decl)->name_type = (existing_name->flags & TYF_NUMBER_LITERAL) ?
                            ty_literal(me, &existing_name->literal.value) : nullptr;
                    }
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
                if (mapped->optional > 0) flags |= PROP_OPTIONAL;
                if (mapped->optional < 0 && (flags & PROP_OPTIONAL)) {
                    flags &= ~(u32)PROP_OPTIONAL;
                    if (compiler_options()->strict_null_checks) flags |= PROP_STRIP_UNDEFINED;
                }
                if (mapped->readonly > 0) flags |= PROP_READONLY;
                if (mapped->readonly < 0) flags &= ~(u32)PROP_READONLY;
                Prop* prop = arena_new(&my arena, Prop);
                *prop = (Prop){
                    .name = name,
                    .flags = flags,
                    .target = template,
                    .mapper = mapper,
                    .mapped = true,
                    .decl = prop_decl_none(),
                };
                *intern_slot(me, &my links, (u64[]){ KEY_PROP_MAPPED_TYPE, (u64)(uintptr_t)prop }, 2) = ty;
                // A fresh symbol, declared where the property it comes from is (tsc's resolveMappedTypeMembers), unless
                // the `as` clause remaps rather than filters (any key: tsc's shouldLinkPropDeclarations, see below)
                Prop const* declared = prop_name == key && !remapping ? from : nullptr;
                while (declared && declared->target && !declared->decl->decl_span_count) declared = declared->target;
                if (declared) {
                    prop->decl = arena_new(&my arena, PropDecl);
                    prop->decl->name_span = declared->decl->name_span;
                    prop->decl->name_form = ty_prop_name_form_of(declared);
                    prop->decl->decl_spans = declared->decl->decl_spans;
                    prop->decl->decl_span_count = declared->decl->decl_span_count;
                    prop->decl->name_is_value = declared->decl->name_is_value;
                    prop_decl_rare_mut(me, prop->decl)->mapped_from = declared;
                    if (modifiers->kind == TY_INTERSECTION) prop_decl_rare_mut(me, prop->decl)->mapped_intersection = modifiers;
                    // (Of a union, declared where each member's property is: tsc's union property symbol)
                    if (modifiers->kind == TY_UNION) {
                        for (u32 m = 0; m < modifiers->composite.types.len; m++) {
                            Ty const* member = ty_apparent_type(me, modifiers->composite.types.data[m]);
                            Prop const* other = (member->flags & TYF_OBJECT) ? ty_get_prop(me, member, name) : nullptr;
                            while (other && other->target && !other->decl->decl_span_count) other = other->target;
                            if (!other || other == declared) continue;
                            for (u32 k = 0; k < other->decl->decl_span_count; k++) {
                                bool seen = false;
                                for (u32 q = 0; q < prop->decl->decl_span_count && !seen; q++) {
                                    seen = span_eq(prop->decl->decl_spans[q], other->decl->decl_spans[k]);
                                }
                                if (!seen) prop_add_decl_spans(me, prop, &other->decl->decl_spans[k], 1);
                            }
                        }
                    }
                }
                // A number key is a number to keyof (tsc's mapped symbol's nameType, getLiteralTypeFromProperty), which
                // the name, a string, doesn't say; an enum member is that member (and prints as one, `[E.A]`)
                if (prop_name->flags & (TYF_NUMBER_LITERAL | TYF_ENUM_LITERAL)) {
                    if (prop->decl == prop_decl_none()) prop->decl = arena_new(&my arena, PropDecl);
                    prop_decl_rare_mut(me, prop->decl)->name_type = ty_regular(me, prop_name);
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
                Ty const* value = ty_mapped_template_instantiated(me, mapped, mapper);
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
    builder_sort_stable(&builder);
    Members* ret = (Members*)builder_finish(me, &builder);
    if (!generic) members_rare_mut(me, ret)->expanded_from = mapped;
    if (generic) members_rare_mut(me, ret)->generic_keys_known = true;
    return ret;
}

static Members const* resolve_members_inner(Checker* me, Ty const* ty);

// A tuple's members (tsc's createTupleTargetType): a property per element before any rest element, a readonly
// `length` (the possible lengths, or number with a rest element), then what the array of its elements has
// The declared members of an array type (Array, ReadonlyArray) whose types mention `this`: sort, reverse, fill,
// copyWithin and every (`this is S[]`). Found once per target, among the members inherited from `array` (the builder's
// props [first, props_count)), which are instantiations of them
typedef struct {
    u32 count;
    Prop const* props[];
} ThisProps;

static bool free_params_have(TyList params, Ty const* param) {
    for (u32 i = 0; i < params.len; i++) if (params.data[i] == param) return true;
    return false;
}

static ThisProps const* array_this_props(Checker* me, MembersBuilder const* builder, u32 first, Ty const* target) {
    void const* known = intern_get(&my links, (u64[]){ KEY_ARRAY_THIS_PROPS, (u64)(uintptr_t)target }, 2);
    if (known) return known;
    Ty const* this_param = target->declared.this_param;
    Prop const* found[256];
    u32 count = 0;
    for (u32 i = first; i < builder->props_count && count < c_arraylen(found); i++) {
        Prop const* declared = builder->props[i]->target;
        if (!declared) continue;
        Ty const* type = prop_type(me, declared);
        // (Or may: a method of an interface declared more than once (Array, in every lib) whose anonymous type has
        // `this` among its free type parameters, tsc's isTypeParameterPossiblyReferenced. Every tuple's `slice` is
        // a type of its own then, `x.slice` of a union of two tuples a union of two)
        bool possibly = type->kind == TY_ANONYMOUS && !type->anonymous.target && !type->anonymous.free_params_unknown &&
            free_params_have(type->anonymous.free_params, this_param);
        if (possibly || (!declared->thisless && ty_mentions_any(me, type, &this_param, 1))) found[count++] = declared;
    }
    ThisProps* ret = arena_alloc(&my arena, sizeof(ThisProps) + sizeof(Prop const*) * (count ? count : 1));
    ret->count = count;
    for (u32 i = 0; i < count; i++) ret->props[i] = found[i];
    // (Looked up again: resolving the types above may have grown the table)
    *intern_slot(me, &my links, (u64[]){ KEY_ARRAY_THIS_PROPS, (u64)(uintptr_t)target }, 2) = ret;
    return ret;
}

// The array's members see the tuple as `this`: `sort(): this` returns the tuple (tsc's resolveTupleTypeMembers takes
// the base with getTypeWithThisArgument). Only the props that mention `this` are instantiated again, with the tuple as
// this-argument; the others stay the (shared) array's, props [first, props_count) of the builder
static void retarget_this_props(Checker* me, MembersBuilder* builder, u32 first, Ty const* array, Ty const* tuple) {
    if (array->kind != TY_REFERENCE) return;
    Ty const* target = array->reference.target;
    if (target->kind != TY_DECLARED || !target->declared.this_param) return;
    ThisProps const* this_props = array_this_props(me, builder, first, target);
    if (!this_props->count) return;
    TyMapper const* mapper = nullptr;
    for (u32 i = first; i < builder->props_count; i++) {
        Prop const* from = builder->props[i];
        bool mentions = false;
        for (u32 j = 0; j < this_props->count && !mentions; j++) mentions = from->target == this_props->props[j];
        if (!mentions) continue;
        if (!mapper) {
            TyList params = target->declared.type_params;
            TyList args = ty_reference_args(me, array);
            if (args.len != params.len) return;
            Ty const** sources = arena_new_n(&my arena, Ty const*, params.len + 1);
            Ty const** targets = arena_new_n(&my arena, Ty const*, params.len + 1);
            for (u32 j = 0; j < params.len; j++) {
                sources[j] = params.data[j];
                targets[j] = args.data[j];
            }
            sources[params.len] = target->declared.this_param;
            targets[params.len] = tuple;
            mapper = ty_mapper_new(me, (TyList){ .data = sources, .len = params.len + 1 },
                (TyList){ .data = targets, .len = params.len + 1 });
        }
        Prop* prop = arena_new(&my arena, Prop);
        *prop = (Prop){ .name = from->name, .flags = from->flags, .parent = from->parent, .target = from->target,
            .mapper = mapper, .decl = prop_decl_none() };
        builder->props[i] = prop;
    }
}

static void push_decimal_order(Prop** out, u32* at, Prop* const* props, u32 index, u32 count) {
    out[(*at)++] = props[index];
    if (!index) return; // (No "01")
    for (u32 digit = 0; digit < 10; digit++) {
        u64 next = (u64)index * 10 + digit;
        if (next >= count) return;
        push_decimal_order(out, at, props, (u32)next, count);
    }
}

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
        prop->decl = (PropDecl*)&ty_prop_decl_value_name; // (Named by a number, nothing else to say: shared)
        prop->name = checker_atom(me, cstr_sv(&name));
        cstr_drop(&name);
        prop->flags = (optional ? PROP_OPTIONAL : 0) | (shape->readonly ? PROP_READONLY : 0);
        prop->type = elements.data[i];
        prop->type_state = LAZY_STATE_DONE;
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
    u32 fixed = builder.props_count;
    Prop* length_prop = prop_new(me);
    length_prop->name = checker_atom(me, c_sv("length"));
    length_prop->flags = shape->readonly ? PROP_READONLY : 0; // Like tsc's, a mutable tuple's length can be assigned
    length_prop->type = length;
    length_prop->type_state = LAZY_STATE_DONE;
    builder_push(&builder, props, length_prop);
    Ty const* array = ty_tuple_as_array(me, tuple);
    if (array) {
        u32 first = builder.props_count;
        builder_add_inherited(me, &builder, array);
        // (The empty tuple too, which is its own target: `[]`'s `sort()` returns `[]`)
        if (tuple->kind == TY_REFERENCE || !shape->arity) retarget_this_props(me, &builder, first, array, tuple);
    }
    // A tuple has no symbol, so tsc 6's getNamedMembers sorts all of its properties: the array's by declaration (the
    // order they are in already), then the element properties and `length`, which have no declaration, by name
    if (array && builder.props_count > fixed + 1) {
        Prop* stack[16];
        Prop** own = fixed + 1 <= c_arraylen(stack) ? stack : malloc(sizeof(Prop*) * (fixed + 1));
        panic_if(!own, "out of memory");
        if (fixed > 10) {
            // ("10" before "2": the indices in the order of their decimal strings, a preorder walk of the digit trie)
            u32 at = 0;
            for (u32 first = 0; first < 10 && first < fixed; first++) push_decimal_order(own, &at, builder.props, first, fixed);
            own[fixed] = builder.props[fixed]; // (`length` sorts after every digit)
        } else {
            memcpy(own, builder.props, sizeof(Prop*) * (fixed + 1));
        }
        u32 inherited = builder.props_count - (fixed + 1);
        memmove(builder.props, builder.props + fixed + 1, sizeof(Prop*) * inherited);
        memcpy(builder.props + inherited, own, sizeof(Prop*) * (fixed + 1));
        if (own != stack) free(own);
    }
    return builder_finish(me, &builder);
}

static Members const* resolve_intersection_members(Checker* me, Ty const* ty);

// tsc's resolveReverseMappedTypeMembers: the source's properties, whose types are inferred on first use (prop_type()),
// and its string index signature's, inferred right away
static Members const* resolve_reverse_mapped_members(Checker* me, Ty const* ty) {
    ReverseMapped const* reverse = ty->anonymous.reverse;
    MappedInfo const* mapped = reverse->mapped;
    ObjectLiteralProp* props;
    u32 count = ty_reverse_mapped_source_props(me, reverse->source, &props);
    MembersBuilder builder = {0};
    // tsc's getLimitedConstraint: with `keyof T & X` for the keys, only the properties X has a key for
    Ty const* limited = nullptr;
    Ty const* constraint = mapped->constraint;
    Ty const* origin = constraint->kind == TY_UNION ? constraint->composite.origin : constraint;
    if (origin && origin->kind == TY_INTERSECTION) {
        Ty const* keyof = ty_keyof(me, reverse->operand);
        Ty const* rest[16];
        u32 rest_count = 0;
        for (u32 i = 0; i < origin->composite.types.len && rest_count < 16; i++) {
            if (origin->composite.types.data[i] != keyof) rest[rest_count++] = origin->composite.types.data[i];
        }
        limited = ty_intersection(me, rest, rest_count, nullptr, (TyList){0});
        if (limited->flags & TYF_NEVER) limited = nullptr;
    }
    for (u32 i = 0; i < count; i++) {
        u32 flags = props[i].flags & (PROP_OPTIONAL | PROP_READONLY);
        if (mapped->optional > 0) flags &= ~(u32)PROP_OPTIONAL; // `?` in the mapped type: T's property needn't be
        if (mapped->readonly > 0) flags &= ~(u32)PROP_READONLY;
        // (The source property's type as tsc's getTypeOfSymbol has it: an optional one's with undefined)
        Ty const* source_type = props[i].type;
        if ((props[i].flags & PROP_OPTIONAL) && compiler_options()->strict_null_checks) {
            source_type = ty_union(me, (Ty const*[]){ source_type, ty_undefined_or_missing(me) }, 2, nullptr, (TyList){0});
        }
        Prop* prop = prop_new_declared(me);
        prop->name = props[i].name;
        prop->flags = flags;
        prop->type_state = LAZY_STATE_NOT_STARTED;
        prop->decl->name_span = props[i].name_span;
        prop->decl->name_is_value = props[i].name_is_value;
        prop->decl->name_form = props[i].name_form;
        if (props[i].name_type) prop_decl_rare_mut(me, prop->decl)->name_type = props[i].name_type;
        // (Numeric and unique symbol keys too: tsc's getLiteralTypeFromProperty with StringOrNumberLiteralOrUnique)
        if (limited && !ty_is_related(me, ty_prop_key_literal(me, prop), limited, TY_RELATION_ASSIGNABLE)) continue;
        // (Its own key is the name's string literal, `1` too: tsc's reverse mapped symbol has no valueDeclaration)
        prop->decl->name_is_value = false;
        prop->decl->asserted = props[i].asserted;
        prop_decl_rare_mut(me, prop->decl)->reverse_of = reverse;
        prop_decl_rare_mut(me, prop->decl)->reverse_source = source_type;
        prop_decl_rare_mut(me, prop->decl)->reverse_declared = props[i].type;
        prop_add_decl_spans(me, prop, props[i].decl_spans, props[i].decl_span_count);
        builder_push(&builder, props, prop);
    }
    free(props);
    // (A reverse mapped type has no symbol: tsc 6's getNamedMembers sorts all of its properties by their declarations,
    // which are the source's, an interface's inherited members by where they are declared too)
    builder_sort_stable(&builder);
    Members const* source_members = ty_members(me, reverse->source);
    for (u32 i = 0; i < source_members->index_count; i++) {
        IndexInfo info = source_members->index[i];
        if (!(info.key->flags & TYF_STRING)) continue;
        // (With the reverse mapped type's own mapped type: tsc's replaceIndexedAccess is for properties only, so a
        // recursive index signature makes a new mapped type, and a new reverse mapped type, at every level)
        info.value = ty_reverse_mapped_prop_type(me, info.value, reverse->mapped, reverse->operand);
        info.param_name = nullptr;
        info.components = nullptr;
        info.component_count = 0;
        builder_push(&builder, index, info);
    }
    return builder_finish(me, &builder);
}

Ty const* ty_reverse_mapped_object(Checker* me, ReverseMapped const* reverse) {
    Ty* fresh = ty_alloc(me, TY_ANONYMOUS, TYF_OBJECT | (reverse->source->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES));
    fresh->anonymous.ctx = (LowerCtx){ .generic = true };
    fresh->anonymous.free_params_unknown = true;
    fresh->anonymous.reverse = reverse;
    fresh->anonymous.is_reverse = true;
    fresh->anonymous.state = arena_new(&my arena, TyObjectState);
    return fresh;
}

static Members const* resolve_members(Checker* me, Ty const* ty) {
    if (ty->kind == TY_INTERSECTION) return resolve_intersection_members(me, ty);
    TyObjectState* state = object_state(ty);
    Members const* members = state && state->unexpanded ? state->unexpanded : resolve_members_inner(me, ty);
    if (!members->mapped) return members;
    // (A mapped type's keys: tsc mostly finds them first through its key parameter's constraint, under that one's
    // pushTypeResolution, see RESOLVING_MAPPED_CONSTRAINT)
    ty_resolving_push(me, RESOLVING_MAPPED_CONSTRAINT, members->mapped);
    members = expand_mapped(me, ty, members);
    ty_resolving_pop(me);
    return members;
}

// A mapped type's declaration (key, constraint, modifiers type) without its members, which a
// generic homomorphic mapped type gets from its type variable's apparent type (tsc's getHomomorphicTypeVariable and
// getIndexTypeForMappedType read the declaration only)
MappedInfo const* ty_mapped_info_unexpanded(Checker* me, Ty const* ty) {
    TyObjectState* state = object_state(ty);
    if (!state) return nullptr;
    if (state->members_state == LAZY_STATE_DONE) return state->members->mapped ? state->members->mapped : members_rare(state->members)->expanded_from;
    if (state->members_state == LAZY_STATE_IN_FLIGHT) return nullptr;
    if (!state->unexpanded) {
        state->members_state = LAZY_STATE_IN_FLIGHT;
        state->unexpanded = resolve_members_inner(me, ty);
        state->members_state = LAZY_STATE_NOT_STARTED;
    }
    return state->unexpanded->mapped;
}

static Members const* resolve_members_inner(Checker* me, Ty const* ty) {
    switch (ty->kind) {
        case TY_DECLARED:
            if (ty->declared.this_param) {
                // As a type of its own it is the class/interface applied to its own type parameters, `this` included
                Ty const* self = ty->declared.type_params.len ? ty_reference(me, ty, ty->declared.type_params) : ty;
                return resolve_members_with_this(me, ty, ty->declared.type_params, self);
            }
            if (ty->declared.raw) return complete_members(me, declared_raw_members(me, ty));
            return resolve_declared_members(me, ty);

        case TY_REFERENCE: {
            Ty const* target = ty->reference.target;
            if (target->declared.kind == TY_DECLARED_TUPLE_TARGET) return resolve_tuple_members(me, ty);
            if (target->declared.this_param) {
                Ty const* this_arg = ty->reference.this_arg ? ty->reference.this_arg : ty;
                return resolve_members_with_this(me, target, ty_reference_args(me, ty), this_arg);
            }
            TyMapper const* mapper = ty_mapper_new(me, target->declared.type_params, ty_reference_args(me, ty));
            if (target->declared.raw) return instantiate_members(me, declared_raw_members(me, target), mapper, false, false);
            return resolve_instantiated_members(me, target, mapper, false);
        }

        case TY_ANONYMOUS: {
            if (ty->anonymous.is_reverse) return resolve_reverse_mapped_members(me, ty);
            if (ty->anonymous.target) {
                return resolve_instantiated_members(me, ty->anonymous.target, ty->anonymous.mapper, ty->anonymous.applies_type_args);
            }
            MembersBuilder builder = {0};
            if (ty->anonymous.merged_value && !ty->anonymous.is_reverse) {
                builder_add_merged_value(me, &builder, ty, ty->anonymous.merged_value);
                // (By declaration, like a declared type's: the exports of a CommonJS `export =` clone among its own)
                builder_sort_stable(&builder);
            } else if (ty->anonymous.function) {
                FunctionImpl const* function = ty->anonymous.function;
                Signature* sig = signature_from_function(me, function, ty->anonymous.ctx);
                if (function_impl_rare(function)->jsdoc_overload_count) {
                    // JSDoc `@overload`s instead of its own
                    for (u32 i = 0; i < function_impl_rare(function)->jsdoc_overload_count; i++) {
                        builder_add_callable(me, &builder, function_impl_rare(function)->jsdoc_overloads[i].callable->function, ty->anonymous.ctx);
                    }
                } else if (sig->flags & SIGNATURE_CONSTRUCT) {
                    builder_push(&builder, construct, sig);
                } else {
                    builder_push(&builder, call, sig);
                }
                // `const f = () => {}; f.x = 1` (tsc merges the variable's exports into the function's symbol)
                Symbol const* host = function_impl_rare(ty->anonymous.function)->expando_host;
                if (host) {
                    Members const* expandos = ty_members(me, ty_namespace_type(me, host));
                    for (u32 i = 0; i < expandos->prop_count; i++) {
                        builder_push(&builder, props, expandos->props[i]);
                    }
                }
            } else if (ty->anonymous.overload_count) {
                for (u32 i = 0; i < ty->anonymous.overload_count; i++) {
                    builder_add_callable(me, &builder, ty->anonymous.overloads[i], ty->anonymous.ctx);
                }
            } else if (ty->anonymous.node->tag == TYPE_CALLABLE) {
                builder_add_callable(me, &builder, ty->anonymous.node->callable->function, ty->anonymous.ctx);
            } else {
                check(ty->anonymous.node->tag == TYPE_INTERFACE);
                // (The early-bound members, then the late-bound ones: see resolve_declared_members())
                builder.skip_computed = true;
                builder_add_interface(me, &builder, &ty->anonymous.node->interface, ty->anonymous.ctx, nullptr);
                builder.skip_computed = false;
                builder.only_late = true;
                builder_add_interface(me, &builder, &ty->anonymous.node->interface, ty->anonymous.ctx, nullptr);
                builder.only_late = false;
                // (tsc 6's getNamedMembers puts the late-bound ones back in declaration order: the members are in it
                // already unless there are some)
                bool late = false;
                for (u32 i = 0; i < builder.props_count && !late; i++) late = name_span_is_late_bound(builder.props[i]->decl->name_span);
                if (late) builder_sort_stable(&builder);
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
// An optional parameter whose type takes undefined under strictNullChecks (tsc's addOptionality), which SigParam.type
// leaves out: `...args?: any[]`
bool ty_sig_param_adds_undefined(SigParam const* param) {
    return param->decl && param->decl->initializer.binding.optional_parameter && compiler_options()->strict_null_checks;
}

static bool is_mixin_constructor(Checker* me, Members const* members) {
    if (members->construct_count != 1) return false;
    Signature const* sig = members->construct[0];
    signature_resolve(me, sig);
    if (sig->type_params.len || sig->param_count != 1 || !sig->params[0].rest) return false;
    Ty const* type = sig->params[0].type;
    if (type->flags & TYF_ANY) return true;
    // (`...args?: any[]` is `any[] | undefined`, no array type: tsc's getElementTypeOfArrayType)
    if (ty_sig_param_adds_undefined(&sig->params[0])) return false;
    return ty_is_array(me, type) && (ty_reference_args(me, type).data[0]->flags & TYF_ANY);
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
        // Its members see the whole intersection as their `this` (tsc's getApparentTypeOfIntersectionType, which
        // getSignaturesOfType goes through): `DD & Base` with `(a: A): X & this` in DD returns `X & DD & Base`. (A
        // reference with a `this` of its own keeps it, like find_prop()'s)
        Ty const* part = types.data[i];
        if (part->kind != TY_TYPE_PARAM && !(part->kind == TY_REFERENCE && part->reference.this_arg)) {
            part = ty_with_this_arg(me, part, ty);
        }
        parts[i] = part_members(me, part);
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
            if (my eager_probe_depth) members_reentered_unguarded(me, ty);
            // An interface's properties while its index signatures are lowered (see resolve_declared_members())
            if (ty->kind == TY_DECLARED) early_declared_members(me, ty);
            if (ty->kind == TY_REFERENCE && ty->reference.target->kind == TY_DECLARED && ty->reference.target->declared.raw &&
                ty->reference.target->declared.raw->members_state == LAZY_STATE_IN_FLIGHT && !ty->reference.target->declared.raw->members
            ) {
                early_declared_members(me, ty->reference.target);
            }
            if (state->members) return state->members;
            // A reference to a declared type whose early members are published: those, instantiated (see
            // early_declared_members())
            if (ty->kind == TY_REFERENCE && ty->reference.target->kind == TY_DECLARED && ty->reference.target->declared.raw &&
                ty->reference.target->declared.raw->members_state == LAZY_STATE_IN_FLIGHT && ty->reference.target->declared.raw->members
            ) {
                state->members = resolve_members(me, ty);
                return state->members;
            }
            // Only a type whose own members depend on its own members gets here (`interface A extends A`)
            members_reentered(me, ty);
            return &empty_members;

        case LAZY_STATE_NOT_STARTED:
            // A deferred reference's arguments come first, before its members are in flight (tsc's
            // resolveTypeReferenceMembers starts with getTypeArguments): arguments that need these members find
            // their own resolution in flight (TS4109), not the members'
            if (ty->kind == TY_REFERENCE && ty->reference.deferred && ty->reference.deferred->state != LAZY_STATE_DONE) {
                ty_reference_args(me, ty);
                if (state->members_state != LAZY_STATE_NOT_STARTED) return ty_members(me, ty);
            }
            state->members_state = LAZY_STATE_IN_FLIGHT;
            if (ty->kind == TY_DECLARED || ty->kind == TY_REFERENCE) {
                members_in_flight_push(me, ty);
                state->members = resolve_members(me, ty);
                // (After the members: asked for again meanwhile, they are what a second round of tsc's would give)
                if (ty->kind == TY_REFERENCE && !my eager_probe_depth) probe_reference(me, ty, 0);
                my members_in_flight_len--;
            } else {
                state->members = resolve_members(me, ty);
            }
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
    while (prop && !span_valid(prop->decl->name_span)) prop = prop->target;
    if (!prop) return (Span){0};
    // An assignment declaration (`this.x = v`) is the whole access (tsc's getErrorSpanForNode of the declaration)
    Symbol const* symbol = prop->decl->symbol;
    if (symbol && symbol->decl_count && symbol->decls[0].kind == SYMBOL_DECL_ASSIGNMENT && span_valid(symbol->decls[0].name) &&
        span_contains_span(symbol->decls[0].name, prop->decl->name_span)
    ) {
        return symbol->decls[0].name;
    }
    // (A JSDoc `@property` too: tsgo reparses it into a PropertySignature, whose error span is its name)
    // A parameter property is the whole parameter, its modifiers first
    if (prop->decl->parameter_property && prop->decl->decl_span_count) return prop->decl->decl_spans[0];
    // An object literal's property assignment is the whole member, modifiers (TS1042) first (getErrorSpanForNode has
    // no case for it)
    ObjectExprItem const* item = prop->decl->item;
    if (prop->decl->literal_own && item && item->tag == OBJECT_ITEM_PROPERTY && span_valid(item->span) &&
        span_contains_span(item->span, prop->decl->name_span)
    ) {
        return span_make_in(item->span, span_start_ptr(item->span), span_end_ptr(prop->decl->name_span));
    }
    return prop->decl->name_span;
}

// Where a property whose annotation reaches its own type reports it: the name of what has the annotation (a getter's
// before a setter's, tsc's getTypeOfAccessors). Invalid if there's no annotation
static Span prop_annotation_circularity_span(Prop const* prop) {
    if (prop->decl->node) return prop->decl->name_span;
    LazyType const* lazy = prop->decl->lazy;
    if (!lazy) return span_null;
    if (lazy->tag == LAZY_TYPE_CLASS_ITEM_FIELD) {
        ClassItem const* item = c_container_of(lazy, ClassItem, field.lazy_type);
        return item->field.has_explicit_type ? item->field.name_span : span_null;
    }
    if (lazy->tag != LAZY_TYPE_CLASS_ITEM_ACCESSOR) return span_null;
    ClassItem const* item = c_container_of(lazy, ClassItem, method.accessor_type);
    ClassItem const* getter = item->method.accessor == CLASS_ACCESSOR_GET ? item : item->method.accessor_getter;
    if (getter && getter->method.func.ret_explicit) return getter->method.name_span;
    if (item->method.accessor == CLASS_ACCESSOR_SET && vec_function_arg_size(&item->method.func.args) &&
        vec_function_arg_at(&item->method.func.args, 0)->initializer.binding.has_explicit_type
    ) {
        return item->method.name_span;
    }
    return span_null;
}

// An object literal setter whose parameter has a type annotation (JSDoc too): the write type of its property
static bool setter_is_annotated(FunctionImpl const* setter) {
    return vec_function_arg_size(&setter->args) && vec_function_arg_at(&setter->args, 0)->initializer.binding.has_explicit_type;
}

static Ty const* instantiated_prop_type(Checker* me, Prop const* prop) {
    MappedInfo const* template_of = prop_decl_rare(prop->target->decl)->template_of;
    Ty const* type = template_of && template_of->template_from ? ty_mapped_template_instantiated(me, template_of, prop->mapper) :
        ty_instantiate(me, prop_type(me, prop->target), prop->mapper);
    // (Only set with strictNullChecks. With exactOptionalPropertyTypes only the missing type goes: tsc's
    // removeMissingOrUndefinedType, `Required<{ a?: number | undefined }>` is `{ a: number | undefined }`)
    if (prop->flags & PROP_STRIP_UNDEFINED) {
        type = compiler_options()->exact_optional_property_types ? ty_remove_missing(me, type, true) : ty_without_undefined(me, type);
    }
    // (An optional property's declared-type view: tsc's getNonMissingTypeOfSymbol)
    return ty_remove_missing(me, type, prop->flags & PROP_OPTIONAL);
}

// How deep instantiated properties are being re-entered (see prop_type()): a backstop, the instantiation depth is
// what stops it
static _Thread_local u32 prop_reentry_depth;

static Ty const* prop_type_slow(Checker* me, Prop* prop);

Ty const* prop_type(Checker* me, Prop const* const_prop) {
    Prop* prop = (Prop*)const_prop; // The lazily resolved type is the one mutable thing about a Prop
    if (prop->type_state == LAZY_STATE_DONE) return prop->type;
    return prop_type_slow(me, prop);
}

// (Out of line: the resolved case above is what nearly every call is)
static __attribute__((noinline)) Ty const* prop_type_slow(Checker* me, Prop* prop) {
    switch (prop->type_state) {
        case LAZY_STATE_DONE: return prop->type;
        case LAZY_STATE_IN_FLIGHT:
            // An instantiated property asked again while instantiated is instantiated again (tsc's
            // getTypeOfInstantiatedSymbol has no guard): `{ "true": Foo<T, Foo<T, B>> }[T]` recurses until the
            // instantiation depth (TS2589), not a circularity
            if (prop->target && !prop->mapped && prop_reentry_depth < 200) {
                prop_reentry_depth++;
                Ty const* again = instantiated_prop_type(me, prop);
                prop_reentry_depth--;
                return again;
            }
            // A symbol's property in flight only below a call's barrier is its symbol's type again (tsc's property is
            // the symbol, whose getTypeOfSymbol pushes it again past resolutionStart): what that reaches finds the cycle
            if (!prop->target && prop->decl->symbol && prop_reentry_depth < 4 && !ty_resolving_find(me, RESOLVING_PROP, prop)) {
                prop_reentry_depth++;
                Ty const* again = ty_type_of_symbol(me, prop->decl->symbol);
                prop_reentry_depth--;
                return again;
            }
            prop->circular = true;
            // (An instantiated property isn't resolved on its own: tsc's getTypeOfInstantiatedSymbol asks its target)
            for (Prop const* p = prop; ; p = p->target) {
                if (!p->target || p->mapped) {
                    ty_resolving_cycle(me, RESOLVING_PROP, p);
                    break;
                }
            }
            return ty_intrinsic(me, TY_CIRCULAR);
        case LAZY_STATE_NOT_STARTED: break;
    }
    prop->type_state = LAZY_STATE_IN_FLIGHT;
    bool resolving = !prop->target || prop->mapped;
    if (resolving) ty_resolving_push(me, RESOLVING_PROP, prop);
    Ty const* type;
    if (prop->flags & (PROP_GETTER | PROP_SETTER) && !prop->target) {
        // (An unannotated getter has the type of its setter's annotation: tsc's getTypeOfAccessors)
        FunctionImpl const* setter = prop_decl_rare(prop->decl)->setter;
        if (setter && !prop->decl->function->ret_explicit && setter_is_annotated(setter)) {
            type = ty_object_literal_accessor_type(me, setter, false);
        } else {
            type = ty_object_literal_accessor_type(me, prop->decl->function, prop->flags & PROP_GETTER);
        }
    } else if (prop->target) {
        type = instantiated_prop_type(me, prop);
    } else if (prop_decl_rare(prop->decl)->overload_count) {
        type = ty_anonymous_from_overloads(me, prop_decl_rare(prop->decl)->overloads, prop_decl_rare(prop->decl)->overload_count, prop->decl->ctx);
    } else if (prop->decl->symbol) {
        type = ty_type_of_symbol(me, prop->decl->symbol);
    } else if (prop->decl->function) {
        type = ty_function_type(me, prop->decl->function);
    } else if (prop->decl->lazy) {
        type = lazy_type_get((LazyType*)prop->decl->lazy);
    } else if (prop_decl_rare(prop->decl)->getter_body) {
        // (tsc's getTypeOfAccessors: the getter's inferred return type, from its body)
        type = ty_object_literal_accessor_type(me, prop_decl_rare(prop->decl)->getter_body, true);
    } else if (prop_decl_rare(prop->decl)->template_of) {
        type = ty_mapped_template(me, prop_decl_rare(prop->decl)->template_of);
    } else if (prop_decl_rare(prop->decl)->reverse_of) {
        ReverseMapped const* reverse = prop_decl_rare(prop->decl)->reverse_of;
        type = ty_reverse_mapped_member_type(me, reverse, prop_decl_rare(prop->decl)->reverse_source);
        // (Declared by the source's declarations: the types baseline reuses their annotation where it still says the
        // same, tsc's serializeTypeForDeclaration)
        // (Or says it with undefined added: an optional one's type has it, see ty_display's annotation_plus_undefined())
        Ty const* declared = prop_decl_rare(prop->decl)->reverse_declared;
        if (type != declared && ty_without_undefined(me, type) != declared) prop->decl->asserted = nullptr;
    } else {
        type = ty_from_node(me, prop->decl->node, prop->decl->ctx);
    }
    // An optional parameter property has the parameter's type, with a real undefined even under
    // exactOptionalPropertyTypes (tsc's addOptionality with isProperty false: it's not a property declaration)
    if (prop->decl->parameter_property && !prop->target && (prop->flags & PROP_OPTIONAL) &&
        compiler_options()->exact_optional_property_types && compiler_options()->strict_null_checks
    ) {
        type = ty_union(me, (Ty const*[]){ type, ty_intrinsic(me, TY_UNDEFINED) }, 2, nullptr, (TyList){0});
    }
    if (resolving && ty_resolving_pop(me)) prop->circular = true;
    if (prop->circular && prop->mapped) {
        // tsc's getTypeOfMappedSymbol when popTypeResolution fails
        prop->type = ty_intrinsic(me, TY_ERROR);
        prop->type_state = LAZY_STATE_DONE;
        cstr name = ty_prop_written_name(me, prop);
        Ty const* mapped_type = intern_get(&my links, (u64[]){ KEY_PROP_MAPPED_TYPE, (u64)(uintptr_t)prop }, 2);
        // (tsc's getTypeOfMappedSymbol sets containsError where pushTypeResolution fails, before it reports)
        if (mapped_type && (mapped_type->flags & TYF_OBJECT)) {
            Members const* expanded = ty_members(me, mapped_type);
            if (members_rare(expanded)->expanded_from) members_rare_mut(me, (Members*)expanded)->contains_error = true;
        }
        cstr mapped = ty_to_cstr(me, mapped_type);
        ty_error_at_current_node_d(me, 2615, "Type of property '", name, "' circularly references itself in mapped type '",
            mapped, "'.");
        c_drop(cstr, &name, &mapped);
        return prop->type;
    }
    // A declared property (or accessor) whose annotation needs its own type (tsc's getTypeOfVariableOrParameterOrProperty
    // / getTypeOfAccessors when popTypeResolution fails). (What the annotation lowered to is already any-like)
    if (prop->circular && !prop->target && !prop->mapped && span_valid(prop_annotation_circularity_span(prop))) {
        Span name = prop_annotation_circularity_span(prop);
        ty_error_at(me, name, 2502, cstr_d("'", span_sv(name), "' is referenced directly or indirectly in its own type annotation."));
    }
    prop->type = type;
    prop->type_state = LAZY_STATE_DONE;
    return type;
}

Ty const* prop_write_type(Checker* me, Prop const* const_prop) {
    Prop* prop = (Prop*)const_prop; // Resolved lazily like the type
    u64 key[2] = { KEY_PROP_WRITE_TYPE, (u64)(uintptr_t)prop };
    if (prop->write_type_known) return intern_get(&my links, key, 2);
    Ty const* type = nullptr;
    if (prop_decl_rare(prop->decl)->write_node) {
        type = ty_from_node(me, prop_decl_rare(prop->decl)->write_node, prop->decl->ctx);
    } else if (prop_decl_rare(prop->decl)->setter && setter_is_annotated(prop_decl_rare(prop->decl)->setter)) {
        type = ty_object_literal_accessor_type(me, prop_decl_rare(prop->decl)->setter, false);
    } else if (prop->target) {
        type = prop_write_type(me, prop->target);
        if (type) type = ty_instantiate(me, type, prop->mapper);
    }
    *intern_slot(me, &my links, key, 2) = type;
    prop->write_type_known = true;
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
    // (Its own type parameters in the outer mapper: being instantiated away, else shadowing the outer ones, tsc's
    // instantiateSignature maps them to fresh ones first)
    bool keep_mapped = sig->instantiates_own_params || sig->drops_mapped_type_params || (sig->flags & SIGNATURE_CONSTRUCT);
    bool any = sig->fresh_type_params;
    for (u32 j = 0; j < outer->len && !any; j++) any = outer->targets[j]->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES;
    for (u32 i = 0; i < params.len && !any; i++) {
        bool mapped = false;
        for (u32 j = 0; j < outer->len; j++) mapped |= outer->sources[j] == params.data[i];
        any = mapped ? !keep_mapped : ty_param_is_bounded(params.data[i]);
    }
    if (!any) return;
    Ty const** fresh = arena_new_n(&my arena, Ty const*, params.len);
    Ty const** sources = arena_new_n(&my arena, Ty const*, outer->len + params.len);
    Ty const** targets = arena_new_n(&my arena, Ty const*, outer->len + params.len);
    u32 len = 0;
    for (u32 j = 0; j < outer->len; j++) {
        bool own = false;
        for (u32 i = 0; i < params.len && !keep_mapped; i++) own |= outer->sources[j] == params.data[i];
        if (own) continue;
        sources[len] = outer->sources[j];
        targets[len++] = outer->targets[j];
    }
    for (u32 i = 0; i < params.len; i++) {
        bool mapped = false;
        for (u32 j = 0; j < outer->len; j++) mapped |= outer->sources[j] == params.data[i];
        fresh[i] = mapped && keep_mapped ? params.data[i] : ty_clone_type_param(me, params.data[i], nullptr, nullptr);
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

// tsc's IsUntypedSignatureInJSFile: a function of a JavaScript file without JSDoc parameter types (or `@type`), not
// called where it is made. Every argument is optional (getMinArgumentCount)
static bool function_is_untyped_js(FunctionImpl const* function) {
    SourceFile const* file = span_source(function->full_span);
    // (A `@type` that isn't a signature, `{function}` or `{number}` on a declaration, leaves it untyped: tsgo's
    // getSignaturesOfSymbol falls back to getSignatureFromDeclaration. So do `@param` tags without types: tsgo asks
    // whether every parameter has no type node, which only a typed tag gives it)
    bool typed = function->jsdoc_type_tagged && !function_impl_rare(function)->jsdoc_signature_type;
    if (!file || !file->is_js || function->is_type || function->is_iife || typed) return false;
    // (Nor with a parameter of its own JSDoc type, `@param {T} x` or `/** @type {string} */ message`)
    for_each (i, vec_function_arg, &function->args) {
        if (i->initializer.binding.has_explicit_type && binding_type(&i->initializer.binding)->from_jsdoc) return false;
    }
    return true;
}

// A JavaScript function's own `@type {F}` naming a function type (tsgo's FullSignature: a declaration's, a method's, a
// function expression's whose comment is its own): F's one call signature, which is the function's (getSignaturesOfSymbol,
// getSignatureOfFullSignatureType). Null if F isn't a single signature (getSingleCallSignature)
static Signature const* jsdoc_tag_single_signature(Checker* me, FunctionImpl const* function, bool* self);

static Signature const* jsdoc_tag_signature(Checker* me, FunctionImpl const* function) {
    bool self = false;
    return jsdoc_tag_single_signature(me, function, &self);
}

// The return type of the function's `@type {F}` signature, which is its return type whatever kind of function it is
// (tsc's getReturnTypeFromAnnotation -> getReturnTypeOfTypeTag). Null if there's no such signature
Ty const* ty_jsdoc_tag_return_type(Checker* me, FunctionImpl const* function) {
    bool self = false;
    Signature const* sig = jsdoc_tag_single_signature(me, function, &self);
    // (Its own type: tsc's return type resolution is circular there, any)
    if (self) return ty_intrinsic(me, TY_ANY);
    return sig ? signature_return_type(me, sig) : nullptr;
}

static Signature const* jsdoc_tag_single_signature(Checker* me, FunctionImpl const* function, bool* self) {
    TypeNode const* node = function_impl_rare(function)->jsdoc_signature_type;
    if (!node) return nullptr;
    Ty const* type = ty_of_node(node);
    if (!(type->flags & TYF_OBJECT)) return nullptr;
    Members const* members = ty_members(me, type);
    if (members->call_count != 1 || members->construct_count || members->index_count || members->prop_count) return nullptr;
    // (Not the function's own signature: `@type {MyClass}` on `function MyClass`, which is TS8030)
    for (Signature const* i = members->call[0]; i; i = i->target) {
        if (i->function == function) {
            *self = true;
            return nullptr;
        }
    }
    return members->call[0];
}

// The renamed elements (`{ a: b }`) of a parameter pattern of a signature without a body, which
// checkVariableLikeDeclaration leaves alone: implicitly any once something asks for the parameter's type (tsc's
// getTypeOfSymbol of the parameter -> getTypeFromBindingElement with reportErrors), here when the signature is resolved
static void report_renamed_bodiless_elements(Checker* me, Binding const* pattern) {
    if (pattern->tag == BINDING_OBJECT) {
        for_each (i, vec_object_binding_item, &pattern->object) {
            if (i->target.rest || i->target.default_value) continue;
            if (i->target.tag != BINDING_IDENTIFIER) {
                report_renamed_bodiless_elements(me, &i->target);
                continue;
            }
            if (span_eq(i->source_span, i->target.content_span)) continue;
            ty_error_at(me, i->target.content_span, 7031, cstr_d("Binding element '", span_name_text(i->target.content_span),
                "' implicitly has an 'any' type."));
        }
    } else if (pattern->tag == BINDING_ARRAY) {
        for_each (i, vec_binding, &pattern->array) {
            if (i->tag == BINDING_OBJECT || i->tag == BINDING_ARRAY) report_renamed_bodiless_elements(me, i);
        }
    }
}

void signature_resolve(Checker* me, Signature const* const_sig) {
    Signature* sig = (Signature*)const_sig;
    if (sig->state != LAZY_STATE_NOT_STARTED) return;
    sig->state = LAZY_STATE_IN_FLIGHT;
    if (!sig->target && sig->function) sig->target = jsdoc_tag_signature(me, sig->function);
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
            Signature const* contextual = ty_function_assigned_contextual_signature(me, function);
            if (contextual) sig->type_params = contextual->type_params;
        }
        LowerCtx ctx = sig->ctx;
        // A function can be anywhere, a function type is lowered knowing whether some type parameter is in scope
        ctx.generic |= !function->is_type || sig->type_params.len > 0;
        ctx.method = false; // Parameter types aren't methods (callbacks are compared with tsc's callback rule)
        u32 count = (u32)vec_function_arg_size(&function->args);
        SigParam* params = arena_new_n(&my arena, SigParam, count ? count : 1);
        u32 iife_arg_count = function->is_iife ? ty_iife_effective_arg_count(me, function) : 0;
        // The arguments it takes are up to its last required parameter: one with a default before it is required too
        // (tsc's minArgumentCount and isOptionalParameter)
        for_each (i, vec_function_arg, &function->args) {
            SigParam* param = &params[i_index];
            param->name = checker_atom(me, span_sv(i->initializer.binding.content_span));
            param->rest = i->rest;
            param->decl = i;
            // (A missing initializer, `n: number=)`, is one: tsc's missing identifier)
            Expr const* initial = initialized_binding_value(&i->initializer);
            param->optional = i->explicit_optional || initial->tag != EXPR_IMPLICIT_ANY || initial->missing;
            param->type = ty_binding_type(me, &i->initializer.binding, ctx);
            if (!function->has_body && !i->initializer.binding.has_explicit_type && compiler_options()->no_implicit_any) {
                report_renamed_bodiless_elements(me, &i->initializer.binding);
            }
            // (tsc's isOptionalParameter: an immediately invoked function's parameter past the arguments)
            // (the minimum argument count goes by the arguments as written, getSignatureFromDeclaration; whether it's
            // optional by the effective ones, tuple spreads expanded)
            bool iife = function->is_iife && !i->initializer.binding.has_explicit_type;
            if (iife && i_index >= iife_arg_count && !i->rest) param->optional = true;
            bool past_written = iife && i_index >= function_impl_rare(function)->iife_arg_count;
            if (!param->optional && !param->rest && !past_written) sig->min_args = (u32)i_index + 1;
        }
        for (u32 j = 0; j < sig->min_args; j++) {
            if (!params[j].optional || params[j].decl->explicit_optional) continue;
            // (Its type still takes undefined, which the default replaces: tsc's addOptionality for a parameter with
            // an initializer)
            params[j].optional = false;
            if (compiler_options()->strict_null_checks) {
                Ty const* parts[2] = { params[j].type, ty_intrinsic(me, TY_UNDEFINED) };
                params[j].type = ty_union(me, parts, 2, nullptr, (TyList){0});
            }
        }
        // (Nor one with a contextual signature: tsgo's isUntypedSignatureInJSFile asks getContextualType)
        if (function_is_untyped_js(function) && !(function->is_expression && ty_function_assigned_contextual_signature(me, function))) {
            sig->min_args = 0;
        }
        if (count && params[0].name == checker_atom(me, c_sv("this"))) {
            // `this: T` is about how the function is called, not an argument
            if (sig->min_args && !params[0].optional) sig->min_args--;
            sig->this_type = params[0].type;
            params++;
            count--;
        } else if (function->is_expression && ty_function_is_context_sensitive(function)) {
            // A function expression without one takes its contextual signature's `this` (tsc's
            // assignContextualParameterTypes gives the signature a thisParameter)
            u64 this_key[2] = { KEY_CONTEXT_THIS, (u64)(uintptr_t)function };
            sig->this_type = intern_get(&my links, this_key, 2);
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

// Whether the signature has a `this` type (Signature.this_type), read off its declaration while it isn't resolved: an
// overloaded call asks of every candidate before trying them, and resolving all of a big overload set (hono's
// HandlerInterface: ~20 signatures of ~10 type parameters with defaults each) instantiates every parameter type tsgo,
// whose instantiated signatures have lazily typed parameters, never looks at
bool ty_signature_has_this(Checker* me, Signature const* sig) {
    Signature const* root = sig;
    while (root->state != LAZY_STATE_DONE && root->target) root = root->target;
    if (root->state == LAZY_STATE_DONE) return root->this_type != nullptr;
    // (A function expression can take its contextual signature's, a JavaScript one its `@type` tag's)
    FunctionImpl const* function = root->function;
    if (!function || function->is_expression || !span_source(function->full_span) || span_source(function->full_span)->is_js) {
        signature_resolve(me, sig);
        return sig->this_type != nullptr;
    }
    if (!vec_function_arg_size(&function->args)) return false;
    // (The name as written, like signature_resolve()'s atom of it)
    csview name = span_sv(vec_function_arg_at(&function->args, 0)->initializer.binding.content_span);
    return name.size == 4 && memcmp(name.buf, "this", 4) == 0;
}

Ty const* signature_return_type(Checker* me, Signature const* const_sig) {
    Signature* sig = (Signature*)const_sig;
    signature_resolve(me, sig);
    switch (sig->ret_state) {
        case LAZY_STATE_DONE: return sig->ret;
        case LAZY_STATE_IN_FLIGHT:
            // An inferred return type in progress: asking it says so, it becomes any (see ty_function_return_type())
            if (!sig->target && sig->function && !sig->function->is_type) ty_function_return_type(me, sig->function);
            ty_resolving_cycle(me, RESOLVING_SIGNATURE_RETURN, sig);
            return ty_intrinsic(me, TY_CIRCULAR);
        case LAZY_STATE_NOT_STARTED: break;
    }
    sig->ret_state = LAZY_STATE_IN_FLIGHT;
    ty_resolving_push(me, RESOLVING_SIGNATURE_RETURN, sig);
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
    if (ty_resolving_pop(me)) {
        // tsc's getReturnTypeOfSignature when popTypeResolution fails: any, and TS2577 at an annotation (an inferred
        // one's TS7023 is ty_function_return_type()'s, an accessor's TS2502 prop_type()'s)
        ret = ty_intrinsic(me, TY_ANY);
        FunctionImpl const* function = sig->function;
        if (!sig->target && function && !function->accessor && (function->is_type || function->ret_explicit) &&
            span_valid(function_impl_ret_span(function))
        ) {
            ty_error_at(me, function_impl_ret_span(function), 2577, cstr_from("Return type annotation circularly references itself."));
        }
    }
    sig->ret = ret;
    sig->ret_state = LAZY_STATE_DONE;
    return ret;
}

static bool signature_return_in_flight(Checker* me, Signature const* sig, bool above_barrier) {
    for (u32 depth = 0; sig && depth < 64; depth++) {
        if (sig->ret_state == LAZY_STATE_DONE) return false;
        if (sig->ret_state == LAZY_STATE_IN_FLIGHT) return true;
        if (!sig->target) {
            if (!sig->function || sig->function->is_type) return false;
            return above_barrier ? ty_resolving_find(me, RESOLVING_FUNCTION_RETURN, sig->function) :
                ty_function_return_in_flight(me, sig->function);
        }
        sig = sig->target;
    }
    return false;
}

bool ty_signature_return_in_flight(Checker* me, Signature const* sig) {
    return signature_return_in_flight(me, sig, false);
}

bool ty_signature_return_resolving(Checker* me, Signature const* sig) {
    return signature_return_in_flight(me, sig, true);
}

// Only ever from an annotation, so asking doesn't infer a return type (TODO: tsc 5.5 infers `x is T` for functions
// that return a narrowing expression)
static LowerCtx predicate_ctx(Signature const* sig) {
    LowerCtx ctx = sig->ctx;
    ctx.generic |= !sig->function->is_type || sig->type_params.len > 0;
    ctx.method = false;
    return ctx;
}

TyPredicate const* signature_predicate(Checker* me, Signature const* const_sig) {
    Signature* sig = (Signature*)const_sig;
    if (sig->predicate_done) {
        if (sig->predicate_in_flight) {
            // tsc's getTypePredicateOfSignature has no guard: asked again while its type is lowered, it lowers it
            // again, which is where an alias in the middle of resolving finds itself (TS2456). Once
            sig->predicate_in_flight = false;
            ty_from_node(me, &sig->function->predicate->type, predicate_ctx(sig));
        }
        return sig->predicate;
    }
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
            .param_name = checker_atom(me, span_sv(node->name)),
        };
        // (`this: T` isn't among the parameters of the signature)
        u32 arg_count = (u32)vec_function_arg_size(&sig->function->args);
        if (predicate->param_index >= 0 && sig->param_count < arg_count) predicate->param_index--;
        if (node->has_type) {
            sig->predicate_in_flight = true;
            predicate->type = ty_from_node(me, &node->type, predicate_ctx(sig));
            sig->predicate_in_flight = false;
        }
        sig->predicate = predicate;
    } else if (sig->function && !sig->function->ret_explicit) {
        // A JavaScript function typed by `@type {F}`: the predicate of F's signature (tsc's getTypePredicateOfSignature
        // with getSignatureOfTypeTag), else one inferred from the body
        bool self = false;
        Signature const* tagged = jsdoc_tag_single_signature(me, sig->function, &self);
        if (tagged && tagged != sig) sig->predicate = signature_predicate(me, tagged);
        // (Only while the return type isn't known to be something other than boolean: tsc's getTypePredicateOfSignature
        // asks `!signature.resolvedReturnType || boolean`. A function expression's is resolved by its first check with a
        // contextual signature, which tttt leaves for later outside inference, so it's asked for now)
        Ty const* known = nullptr;
        if (!sig->predicate && sig->param_count) {
            known = sig->ret_state == LAZY_STATE_DONE ? sig->ret : ty_function_return_known(me, sig->function);
            if (!known && ty_function_assigned_contextual_signature(me, sig->function) &&
                !ty_function_return_in_flight(me, sig->function)) {
                known = ty_function_return_type(me, sig->function);
            }
        }
        if (!sig->predicate && sig->param_count && (!known || (known->flags & TYF_BOOLEAN))) {
            sig->predicate = ty_infer_type_predicate(me, sig->function);
        }
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
    return ty_object_literal_of(me, props, count, index, index_count, kind, nullptr);
}

// `expr`, when not null, is the literal expression the type is of: a type of its own (an empty literal has nothing that
// tells it apart from another, but tsc makes a type per literal, whose id orders unions)
Ty const* ty_object_literal_of(
    Checker* me, ObjectLiteralProp const* props, u32 count, IndexInfo const* index, u32 index_count, ObjectLiteralKind kind,
    void const* expr
) {
    return ty_object_literal_alternative(me, props, count, index, index_count, kind, expr, 0);
}

Ty const* ty_object_literal_alternative(
    Checker* me, ObjectLiteralProp const* props, u32 count, IndexInfo const* index, u32 index_count, ObjectLiteralKind kind,
    void const* expr, u32 alternative
) {
    WordBuf words = {0};
    words_push(&words, KEY_STRUCTURAL);
    words_push(&words, kind);
    if (expr) {
        words_push(&words, 4);
        words_push(&words, (u64)(uintptr_t)expr);
    }
    if (alternative) {
        words_push(&words, 5);
        words_push(&words, alternative);
    }
    for (u32 i = 0; i < count; i++) {
        if (props[i].reuse) {
            words_push(&words, 2);
            words_push(&words, (u64)(uintptr_t)props[i].reuse);
            continue;
        }
        words_push(&words, 1);
        words_push(&words, (u64)(uintptr_t)props[i].name);
        words_push(&words, props[i].flags | (u64)props[i].own << 32);
        words_push(&words, (u64)(uintptr_t)props[i].name_span.buf);
        words_push(&words, props[i].type ? props[i].type->id : (u64)(uintptr_t)props[i].accessor);
        // (How the name prints, and an accessor pair's setter)
        words_push(&words, props[i].name_form | (props[i].name_type ? (u64)props[i].name_type->id << 8 : 0));
        if (props[i].setter) words_push(&words, (u64)(uintptr_t)props[i].setter);
        if (props[i].parent) words_push(&words, (u64)(uintptr_t)props[i].parent | 1);
        // (The annotation the types baseline reuses: two literals differing only there print differently)
        if (props[i].asserted) words_push(&words, (u64)(uintptr_t)props[i].asserted | 2);
    }
    for (u32 i = 0; i < index_count; i++) {
        words_push(&words, 3);
        words_push(&words, index[i].key->id);
        words_push(&words, index[i].value->id);
        words_push(&words, (u64)(uintptr_t)index[i].param_name << 1 | index[i].readonly);
        // (The computed members it comes from print: `{ [k]: T }` of one literal isn't another's)
        for (u32 j = 0; j < index[i].component_count; j++) words_push(&words, (u64)(uintptr_t)index[i].components[j].name);
    }
    Ty const* ret = intern_get(&my links, words.data, words.len);
    bool js = kind & OBJECT_JS;
    bool no_inferable_index = kind & OBJECT_NO_INFERABLE_INDEX;
    kind &= ~(OBJECT_JS | OBJECT_NO_INFERABLE_INDEX);
    if (!ret) {
        MembersBuilder builder = {0};
        bool widens = kind == OBJECT_LITERAL_FRESH || kind == OBJECT_JSX_ATTRIBUTES_FRESH || kind == OBJECT_LITERAL_REGULAR;
        u32 flags = TYF_OBJECT | (widens ? TYF_FRESH_OBJECT : 0);
        for (u32 i = 0; i < count; i++) {
            if (props[i].reuse) {
                flags |= prop_type(me, props[i].reuse)->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES;
                builder_push(&builder, props, (Prop*)props[i].reuse);
                continue;
            }
            Prop* prop = prop_new_declared(me);
            prop->name = props[i].name;
            prop->flags = props[i].flags;
            prop->type = props[i].type;
            prop->type_state = props[i].type ? LAZY_STATE_DONE : LAZY_STATE_NOT_STARTED;
            prop->decl->function = props[i].accessor;
            if (props[i].setter) prop_decl_rare_mut(me, prop->decl)->setter = props[i].setter;
            prop->decl->name_span = props[i].name_span;
            prop->decl->name_is_value = props[i].name_is_value;
            prop->decl->literal_own = props[i].own;
            prop->decl->name_form = props[i].name_form;
            if (props[i].name_type) prop_decl_rare_mut(me, prop->decl)->name_type = props[i].name_type;
            prop->decl->asserted = props[i].asserted;
            prop->decl->item = props[i].item;
            if (props[i].spread_variable) {
                prop_decl_rare_mut(me, prop->decl)->spread_variable = props[i].spread_variable;
                prop_decl_rare_mut(me, prop->decl)->spread_namespace = props[i].spread_namespace;
                prop_decl_rare_mut(me, prop->decl)->spread_fresh = props[i].spread_fresh;
            }
            prop->parent = props[i].parent;
            prop_add_shared_decl_spans(me, prop, props[i].decl_spans, props[i].decl_span_count);
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
        if (kind != OBJECT_SYNTHETIC && !compiler_options()->strict_null_checks) {
            // (A spread property's ContainsWideningType doesn't propagate: tsc's checkObjectLiteral only collects its own
            // properties' flags)
            for (u32 i = 0; i < count; i++) {
                if (props[i].reuse || !props[i].type) continue;
                u32 xflags = props[i].type->xflags;
                fresh->xflags |= props[i].own || props[i].own_merged ? xflags : xflags & ~(u32)TYX_CONTAINS_WIDENING;
            }
            for (u32 i = 0; i < index_count; i++) fresh->xflags |= index[i].value->xflags;
        }
        fresh->anonymous.ctx = (LowerCtx){ .generic = true };
        // (Of a literal outside every function and generic declaration: none, so it's never instantiated. Keyed by
        // every mapping it's instantiated with otherwise: a top-level zod schema literal had 280k instances in mcp-sdk)
        Expr const* literal = kind != OBJECT_SYNTHETIC ? expr : nullptr;
        fresh->anonymous.free_params_unknown = !(literal && literal->tag == EXPR_OBJECT && literal->no_enclosing_type_params);
        fresh->anonymous.object_literal = kind != OBJECT_SYNTHETIC;
        fresh->anonymous.no_inferable_index = no_inferable_index;
        fresh->anonymous.regular = kind == OBJECT_LITERAL_REGULAR;
        fresh->anonymous.spread_alternative = (uint16_t)alternative;
        fresh->anonymous.jsx_attributes = kind == OBJECT_JSX_ATTRIBUTES || kind == OBJECT_JSX_ATTRIBUTES_FRESH;
        fresh->anonymous.js_literal = js;
        if (kind != OBJECT_SYNTHETIC) fresh->anonymous.literal_expr = expr;
        fresh->anonymous.state = arena_new(&my arena, TyObjectState);
        builder_sort_stable(&builder);
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

// (tsc's isObjectLiteralType, which a widened one no longer is: getWidenedTypeOfObjectLiteral drops
// ObjectFlags.ObjectLiteral. `[left, { id: 1, value: "" }]` with `const left = { value: "" }` doesn't give left's type
// `id?: undefined`)
static bool is_object_literal_type(Ty const* ty) {
    return ty_is_unwidened_object_literal(ty);
}

// tsc's getSiblingsOfContext
static TyBuf const* context_siblings(Checker* me, WideningCtx* ctx) {
    if (ctx->siblings_done) return &ctx->siblings;
    ctx->siblings_done = true;
    TyBuf const* parent = context_siblings(me, ctx->parent);
    for (u32 i = 0; i < parent->len; i++) {
        if (!is_object_literal_type(parent->data[i])) continue;
        Prop const* prop = ty_get_prop(me, parent->data[i], ctx->name);
        if (prop && !prop->decl->function) push_members(&ctx->siblings, prop_type(me, prop));
    }
    return &ctx->siblings;
}

// HACK: tsc's ObjectFlags.ContainsSpread, as a property that was spread into the literal (`{ ...{} }` doesn't count)
static bool contains_spread(Checker* me, Ty const* literal) {
    Members const* members = ty_members(me, literal);
    for (u32 i = 0; i < members->prop_count; i++) {
        if (!members->props[i]->decl->literal_own) return true;
    }
    return false;
}

// (Only an unwidened one: tsc's getWidenedTypeOfObjectLiteral drops ObjectFlags.ObjectLiteral, so a declared `let x = s ??
// { b: 1 }` lacks `x.a`)
bool ty_is_object_literal_without_spread(Checker* me, Ty const* ty) {
    return ty_is_unwidened_object_literal(ty) && !contains_spread(me, ty);
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
        if (!prop->decl->function && ctx) {
            WideningCtx child = { .parent = ctx, .name = prop->name };
            type = widen_with_context(me, prop_type(me, prop), &child);
            widening_ctx_drop(&child);
        } else if (!prop->decl->function) {
            type = ty_widened_objects(me, prop_type(me, prop));
        }
        props[i] = (ObjectLiteralProp){
            .name = prop->name,
            .flags = prop->flags,
            .type = type,
            .accessor = prop->decl->function,
            .setter = prop_decl_rare(prop->decl)->setter,
            .name_span = prop->decl->name_span,
            .decl_spans = prop->decl->decl_spans,
            .decl_span_count = prop->decl->decl_span_count,
            .name_is_value = prop->decl->name_is_value,
            .asserted = prop->decl->asserted,
            .item = prop->decl->item,
            .spread_variable = prop_decl_rare(prop->decl)->spread_variable,
            .spread_namespace = prop_decl_rare(prop->decl)->spread_namespace,
            .spread_fresh = prop_decl_rare(prop->decl)->spread_fresh,
            .own = prop->decl->literal_own,
            .name_form = ty_prop_name_form_of(prop),
            .name_type = prop_decl_rare(prop->decl)->name_type,
            .parent = !prop->decl->function && type == prop_type(me, prop) ? prop->parent : nullptr,
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
        // (One symbol per name for the whole program, made of the first property it was needed for: tsc's
        // undefinedProperties cache. Only its declarations show, in the symbols baseline)
        void const** cached = intern_slot(me, &my links, (u64[]){ KEY_UNDEFINED_PROPERTY, (u64)(uintptr_t)prop->name }, 2);
        if (!*cached) *cached = prop;
        Prop const* first = *cached;
        props[count++] = (ObjectLiteralProp){
            .name = prop->name,
            // (Readonly like that first property: tsc's createSymbolWithType keeps CheckFlags.Readonly)
            .flags = PROP_OPTIONAL | (first->flags & PROP_READONLY),
            .type = ty_intrinsic(me, exact ? TY_NEVER : TY_UNDEFINED),
            .name_span = prop->decl->name_span,
            .name_form = ty_prop_name_form_of(prop),
            .name_type = prop_decl_rare(prop->decl)->name_type,
            .decl_spans = first->decl->decl_spans,
            .decl_span_count = first->decl->decl_span_count,
            .name_is_value = prop->decl->name_is_value,
        };
    }
    IndexInfo index[8] = {0};
    u32 index_count = members->index_count < c_arraylen(index) ? members->index_count : c_arraylen(index);
    for (u32 i = 0; i < index_count; i++) {
        index[i] = members->index[i];
        index[i].value = ty_widened_objects(me, index[i].value);
    }
    // (JSX attributes stay what they are: hyphenated names are still never excess nor weak-type failures)
    Ty const* ret = ty_object_literal_alternative(me, props, count, index, index_count,
        (ty->anonymous.jsx_attributes ? OBJECT_JSX_ATTRIBUTES : OBJECT_LITERAL) | (ty->anonymous.js_literal ? OBJECT_JS : 0),
        ty->anonymous.literal_expr, ty->anonymous.spread_alternative);
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
        // Widening an empty object literal makes it take in everything but null and undefined, so the union is reduced
        // then (tsc's getWidenedTypeWithContext: `{} | string` is `{}`)
        bool empty_object = false;
        for (u32 i = 0; changed && i < parts.len && !empty_object; i++) empty_object = ty_is_empty_object_type(me, parts.data[i]);
        Ty const* ret = !changed ? ty : empty_object ? ty_union_subtype_reduced(me, parts.data, parts.len) :
            ty_union(me, parts.data, parts.len, ty->alias, ty->alias_args);
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
    check(ty->kind == TY_ANONYMOUS && (ty->flags & TYF_FRESH_OBJECT));
    return widen_object_literal(me, ty, nullptr);
}

static Ty const* widen_nullable(Checker* me, Ty const* ty, u32 depth) {
    if (depth > 16 || !(ty->xflags & TYX_WIDENS)) return ty;
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
            Ty const* type = prop->decl->function ? nullptr : prop_type(me, prop);
            Ty const* widened = type ? widen_nullable(me, type, depth + 1) : type;
            changed |= widened != type;
            props[i] = (ObjectLiteralProp){
                .name = prop->name,
                .flags = prop->flags,
                .type = widened,
                .accessor = prop->decl->function,
                .setter = prop_decl_rare(prop->decl)->setter,
                .name_span = prop->decl->name_span,
                .decl_spans = prop->decl->decl_spans,
                .decl_span_count = prop->decl->decl_span_count,
                .name_is_value = prop->decl->name_is_value,
                .asserted = prop->decl->asserted,
                .item = prop->decl->item,
                .own = prop->decl->literal_own,
                .name_form = ty_prop_name_form_of(prop),
                .name_type = prop_decl_rare(prop->decl)->name_type,
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
            ret = ty_object_literal_alternative(me, props, members->prop_count, index, index_count,
                (ty->anonymous.jsx_attributes ? (ty->flags & TYF_FRESH_OBJECT ? OBJECT_JSX_ATTRIBUTES_FRESH : OBJECT_JSX_ATTRIBUTES) :
                ty->anonymous.regular ? OBJECT_LITERAL_REGULAR : ty->flags & TYF_FRESH_OBJECT ? OBJECT_LITERAL_FRESH : OBJECT_LITERAL) |
                (ty->anonymous.js_literal ? OBJECT_JS : 0),
                ty->anonymous.literal_expr, ty->anonymous.spread_alternative);
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
    // An intersection member by member (`{ a: "x" } & T` of a spread of a generic: tsc's getWidenedTypeWithContext)
    if (ty->kind == TY_INTERSECTION) {
        TyList types = ty->composite.types;
        u32 i = 0;
        while (i < types.len && !(types.data[i]->flags & TYF_FRESH_OBJECT)) i++;
        if (i == types.len) return ty;
        TyBuf parts = {0};
        for (u32 j = 0; j < types.len; j++) ty_buf_push(&parts, ty_widened_objects(me, types.data[j]));
        Ty const* ret = ty_intersection(me, parts.data, parts.len, nullptr, (TyList){0});
        ty_buf_drop(&parts);
        return ret;
    }
    if (!(ty->flags & TYF_FRESH_OBJECT)) return ty;
    u64 key[2] = { KEY_WIDENED_OBJECTS, (u64)(uintptr_t)ty };
    void const* known = intern_get(&my links, key, 2);
    if (known) return known;
    Ty const* ret = widen_objects(me, ty);
    *intern_slot(me, &my links, key, 2) = ret;
    return ret;
}

Ty const* ty_regular_object_literal(Checker* me, Ty const* ty) {
    if (!ty_is_fresh_object(ty)) return ty;
    // (JSX attributes stay what widening makes them, as before)
    if (ty->anonymous.jsx_attributes) return ty_widened_objects(me, ty);
    u64 key[2] = { KEY_REGULAR_OBJECT_LITERAL, (u64)(uintptr_t)ty };
    void const* known = intern_get(&my links, key, 2);
    if (known) return known;
    Members const* members = ty_members(me, ty);
    ObjectLiteralProp* props = calloc(members->prop_count ? members->prop_count : 1, sizeof(ObjectLiteralProp));
    for (u32 i = 0; i < members->prop_count; i++) {
        Prop const* prop = members->props[i];
        props[i] = (ObjectLiteralProp){
            .name = prop->name,
            .flags = prop->flags,
            .type = prop->decl->function ? nullptr : ty_regular_object_literal(me, prop_type(me, prop)),
            .accessor = prop->decl->function,
            .setter = prop_decl_rare(prop->decl)->setter,
            .name_span = prop->decl->name_span,
            .decl_spans = prop->decl->decl_spans,
            .decl_span_count = prop->decl->decl_span_count,
            .name_is_value = prop->decl->name_is_value,
            .asserted = prop->decl->asserted,
            .item = prop->decl->item,
            .spread_variable = prop_decl_rare(prop->decl)->spread_variable,
            .spread_namespace = prop_decl_rare(prop->decl)->spread_namespace,
            .spread_fresh = prop_decl_rare(prop->decl)->spread_fresh,
            .own = prop->decl->literal_own,
            .name_form = ty_prop_name_form_of(prop),
            .name_type = prop_decl_rare(prop->decl)->name_type,
        };
    }
    IndexInfo index[8] = {0};
    u32 index_count = members->index_count < c_arraylen(index) ? members->index_count : c_arraylen(index);
    for (u32 i = 0; i < index_count; i++) index[i] = members->index[i];
    Ty const* ret = ty_object_literal_alternative(me, props, members->prop_count, index, index_count,
        OBJECT_LITERAL_REGULAR | (ty->anonymous.js_literal ? OBJECT_JS : 0), ty->anonymous.literal_expr,
        ty->anonymous.spread_alternative);
    free(props);
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

// The constraint as declared (tsc's getConstraintFromTypeParameter)
static Ty const* declared_param_constraint(Checker* me, Ty const* param) {
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
            // Merged declarations share the type parameter: the first one that constrains it does (tsc's
            // getConstraintDeclaration)
            Symbol const* symbol = param->param.symbol;
            for (u32 i = 0; !node && symbol && i < symbol->decl_count; i++) {
                if (symbol->decls[i].kind != SYMBOL_DECL_TYPE_PARAMETER) continue;
                GenericItem const* item = symbol->decls[i].node;
                if (item && item->extends_explicit) node = &item->extends;
            }
        }
        state->constraint = node ? ty_from_node(me, node, (LowerCtx){ .generic = true }) : nullptr;
        // A declared `extends any` is `extends unknown` (tsc's getConstraintFromTypeParameter; its keyofConstraintType for
        // a mapped type's key isn't done here). One that becomes any by instantiation stays any, and any is related to
        // everything: `<T extends E>` of `Ref<any>` takes any other type parameter
        if (state->constraint == ty_intrinsic(me, TY_ANY) && node != state->constraint_node) {
            state->constraint = ty_intrinsic(me, TY_UNKNOWN_);
        }
        if (!node && state->is_infer) state->constraint = ty_implied_infer_constraint(me, param);
        state->constraint_state = LAZY_STATE_DONE;
    }
    // In flight means the constraint is circular (`T extends T`), tsc reports TS2313 and carries on without one
    return state->constraint_state == LAZY_STATE_DONE ? state->constraint : nullptr;
}


// tsc's getConstraintOfTypeParameter: none if the base constraint is circular (`T extends T`, `T extends U, U extends
// T`, `T extends Foo | T["k"]`). A copy made by instantiation is circular when its original is
bool ty_param_constraint_is_circular(Checker* me, Ty const* param) {
    check(param->kind == TY_TYPE_PARAM);
    TyParamState* state = param->param.state;
    if (!state->circular) {
        if (state->clone_of) {
            state->circular = ty_param_constraint_is_circular(me, state->clone_of) ? 2 : 1;
        } else {
            bool circular = !ty_has_non_circular_base_constraint(me, param);
            // (Asked while its base constraint is computed: circular for now, the answer is known once that's done)
            if (ty_base_constraint_in_flight(me, param)) return circular;
            if (!state->circular) state->circular = circular ? 2 : 1;
        }
    }
    return state->circular == 2;
}

Ty const* ty_declared_param_constraint(Checker* me, Ty const* param) { return declared_param_constraint(me, param); }

Ty const* ty_param_constraint(Checker* me, Ty const* param) {
    // The declared constraint is lowered under the base constraint's push (tsc's getResolvedBaseConstraint)
    if (!param->param.state->circular && !param->param.state->clone_of && !param->param.state->is_this &&
        param->param.state->constraint_state == LAZY_STATE_NOT_STARTED) {
        ty_param_constraint_is_circular(me, param);
    }
    Ty const* constraint = declared_param_constraint(me, param);
    if (!constraint || param->param.state->circular == 1) return constraint;
    return ty_param_constraint_is_circular(me, param) ? nullptr : constraint;
}

// The declaration of a type parameter's default: the first of its declarations that has one (tsc's
// getResolvedTypeParameterDefault), a later interface declaration's `interface I<T, R = undefined>` too
static GenericItem const* default_item(Ty const* param) {
    GenericItem const* item = param->param.state->item;
    if (item && item->fallback_explicit) return item;
    Symbol const* symbol = param->param.symbol;
    for (u32 i = 0; symbol && i < symbol->decl_count; i++) {
        GenericItem const* other = symbol->decls[i].kind == SYMBOL_DECL_TYPE_PARAMETER ? symbol->decls[i].node : nullptr;
        if (other && other->fallback_explicit) return other;
    }
    return nullptr;
}

bool ty_param_has_default(Ty const* param) {
    check(param->kind == TY_TYPE_PARAM);
    TyParamState const* state = param->param.state;
    if (state->clone_of) return ty_param_has_default(state->clone_of);
    return state->item && default_item(param);
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
        GenericItem const* item = state->item ? default_item(param) : nullptr;
        if (item) state->fallback = ty_from_node(me, item->fallback, (LowerCtx){ .generic = true });
        // (tsc's getResolvedTypeParameterDefault: asked for while being resolved, it's circularConstraintType)
        if (state->fallback_circular) state->fallback = nullptr;
        state->fallback_state = LAZY_STATE_DONE;
    } else if (state->fallback_state == LAZY_STATE_IN_FLIGHT && !state->clone_of) {
        state->fallback_circular = true;
    }
    return state->fallback_state == LAZY_STATE_DONE ? state->fallback : nullptr;
}

static Ty const* compute_mapped_template(Checker* me, MappedInfo const* mapped) {
    return mapped->template_node ? ty_from_node(me, mapped->template_node, mapped->template_ctx) :
        ty_instantiate(me, ty_mapped_template(me, mapped->template_from), mapped->template_mapper);
}

// The template with the key parameter mapped (`mapper`): an instantiated mapped type's template is its declaration's
// instantiated once, with every instantiation's mapper and the key's composed, like tsc's addMemberForKeyTypeWorker
// (instantiateType(templateType, appendTypeMapping(type.mapper, typeParameter, keyType))). Instantiating in steps
// resolves what the first step makes non-generic but incomplete: `GCK<T, "type", K>["params"]` with T a union and K
// still generic reads `params` of every member (unknown) before K picks the one that has it
Ty const* ty_mapped_template_instantiated(Checker* me, MappedInfo const* mapped, TyMapper const* mapper) {
    while (mapped->template_from && !mapped->template_node) {
        mapper = ty_compose_mappers(me, mapped->template_mapper, mapper);
        mapped = mapped->template_from;
    }
    return ty_instantiate(me, ty_mapped_template(me, mapped), mapper);
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
