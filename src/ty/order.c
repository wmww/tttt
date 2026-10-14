// tsc 6/7's stable type ordering (stableTypeOrdering, checker.ts's compareTypes): the order of a union's members,
// independent of type creation order. PROTOTYPE: ties tsc breaks with mappers or nodes tttt doesn't keep fall back to
// type ids
#include "ty/ty_internal.h"
#include "scope.h"
#include "source_file.h"

// tsc 6's TypeFlags bit (getSortOrderFlags: enum-like unit types are TypeFlags.Enum)
enum {
    RANK_ANY = 0, RANK_UNKNOWN, RANK_UNDEFINED, RANK_NULL, RANK_VOID, RANK_STRING, RANK_NUMBER, RANK_BIGINT,
    RANK_BOOLEAN, RANK_ES_SYMBOL, RANK_STRING_LITERAL, RANK_NUMBER_LITERAL, RANK_BIGINT_LITERAL, RANK_BOOLEAN_LITERAL,
    RANK_UNIQUE_ES_SYMBOL, RANK_ENUM_LITERAL, RANK_ENUM, RANK_NON_PRIMITIVE, RANK_NEVER, RANK_TYPE_PARAMETER,
    RANK_OBJECT, RANK_INDEX, RANK_TEMPLATE_LITERAL, RANK_STRING_MAPPING, RANK_SUBSTITUTION, RANK_INDEXED_ACCESS,
    RANK_CONDITIONAL, RANK_UNION, RANK_INTERSECTION,
};

u32 ty_order_rank(Ty const* t) {
    switch ((TyKind)t->kind) {
        case TY_INTRINSIC:
        case TY_LITERAL: {
            u32 f = t->flags;
            if (f & (TYF_ENUM_LITERAL | TYF_ENUM)) return RANK_ENUM;
            if (f & TYF_ANY) return RANK_ANY;
            if (f & TYF_UNKNOWN) return RANK_UNKNOWN;
            if (f & TYF_UNDEFINED) return RANK_UNDEFINED;
            if (f & TYF_NULL) return RANK_NULL;
            if (f & TYF_VOID) return RANK_VOID;
            if (f & TYF_STRING_LITERAL) return RANK_STRING_LITERAL;
            if (f & TYF_NUMBER_LITERAL) return RANK_NUMBER_LITERAL;
            if (f & TYF_BIGINT_LITERAL) return RANK_BIGINT_LITERAL;
            if (f & TYF_BOOLEAN_LITERAL) return RANK_BOOLEAN_LITERAL;
            if (f & TYF_UNIQUE_ES_SYMBOL) return RANK_UNIQUE_ES_SYMBOL;
            if (f & TYF_STRING) return RANK_STRING;
            if (f & TYF_NUMBER) return RANK_NUMBER;
            if (f & TYF_BIGINT) return RANK_BIGINT;
            if (f & TYF_ES_SYMBOL) return RANK_ES_SYMBOL;
            if (f & TYF_NON_PRIMITIVE) return RANK_NON_PRIMITIVE;
            if (f & TYF_NEVER) return RANK_NEVER;
            return RANK_ANY;
        }
        case TY_TYPE_PARAM: return RANK_TYPE_PARAMETER;
        case TY_DECLARED:
        case TY_REFERENCE:
        case TY_ANONYMOUS:
        case TY_EVOLVING_ARRAY: return RANK_OBJECT;
        case TY_UNION: return RANK_UNION;
        case TY_INTERSECTION: return RANK_INTERSECTION;
        case TY_KEYOF: return RANK_INDEX;
        case TY_INDEXED: return RANK_INDEXED_ACCESS;
        case TY_CONDITIONAL: return RANK_CONDITIONAL;
        case TY_SUBSTITUTION: return RANK_SUBSTITUTION;
        case TY_TEMPLATE_LITERAL: return RANK_TEMPLATE_LITERAL;
        case TY_STRING_MAPPING: return RANK_STRING_MAPPING;
    }
    return RANK_ANY;
}

static int cmp_u64(u64 a, u64 b) { return a < b ? -1 : a > b; }

static int cmp_sv(csview a, csview b) {
    isize n = a.size < b.size ? a.size : b.size;
    int c = n > 0 ? memcmp(a.buf, b.buf, (size_t)n) : 0;
    if (c) return c < 0 ? -1 : 1;
    return cmp_u64((u64)a.size, (u64)b.size);
}

// A declaration's place in the program: tsc's compareNodes (file order, then position)
typedef struct OrderPos {
    u32 file;
    u32 offset;
} OrderPos;

static bool span_pos(Span s, OrderPos* out) {
    if (!span_source(s) || !s.buf) return false;
    *out = (OrderPos){ span_source(s)->program_order, (u32)(s.buf - cstr_str(&span_source(s)->contents)) };
    return true;
}

static int cmp_pos(OrderPos a, OrderPos b) {
    if (a.file != b.file) return a.file < b.file ? -1 : 1;
    return a.offset < b.offset ? -1 : a.offset > b.offset;
}

// tsc's compareNodes: file order, then position; a missing node last (0: tie, or neither has one)
static int cmp_node_spans(Span a, Span b, bool ha, bool hb) {
    OrderPos pa = {0}, pb = {0};
    ha = ha && span_pos(a, &pa);
    hb = hb && span_pos(b, &pb);
    if (ha && hb) return cmp_pos(pa, pb);
    if (ha) return -1;
    if (hb) return 1;
    return 0;
}

// Where a deferred reference's node starts (tsc's node.pos, compared within a file): its first token
static bool type_node_start(TypeNode const* node, Span* out) {
    for (int depth = 0; node && depth < 16; depth++) {
        switch (node->tag) {
            case TYPE_IDENTIFIER: *out = identifier_type_span(node->identifier); return span_valid(*out);
            case TYPE_TUPLE: *out = node->tuple.span; return span_valid(*out);
            case TYPE_INTERFACE: *out = node->interface.span; return span_valid(*out);
            case TYPE_ARRAY: node = node->array.element; break;
            case TYPE_UNION:
            case TYPE_INTERSECTION:
                node = vec_type_size(&node->composite) ? vec_type_at(&node->composite, 0) : nullptr;
                break;
            default: return false;
        }
    }
    return false;
}

// A symbol's name as tsc's escapedName: a module's default export is `default` whatever its declaration is named
// (declareSymbol), tttt's Symbol.name is the name as written
static csview escaped_name(Symbol const* s) {
    return s->is_default_export ? c_sv("default") : s->name;
}

// Where a symbol's first declaration is: a source file's module symbol is declared by the file itself (position 0)
static bool symbol_first_pos(Symbol const* s, OrderPos* out) {
    if (s->is_module && s->module && s->module->file) {
        *out = (OrderPos){ s->module->file->program_order, 0 };
        return true;
    }
    return s->decl_count && span_pos(s->decls[0].full, out);
}

// tsc's compareSymbols, minus symbol ids (0: tie)
int ty_compare_symbols(Symbol const* a, Symbol const* b) {
    if (a == b) return 0;
    if (!a) return 1;
    if (!b) return -1;
    OrderPos pa = {0}, pb = {0};
    bool ha = symbol_first_pos(a, &pa);
    bool hb = symbol_first_pos(b, &pb);
    if (ha && hb) {
        int c = cmp_pos(pa, pb);
        if (c) return c;
    } else if (ha) {
        return -1;
    } else if (hb) {
        return 1;
    }
    return cmp_sv(escaped_name(a), escaped_name(b));
}

// tsc's compareSymbols of two declared things known by their first declaration's span (invalid: none) and name
int ty_compare_declarations(Span a, csview a_name, Span b, csview b_name) {
    OrderPos pa = {0}, pb = {0};
    bool ha = span_pos(a, &pa), hb = span_pos(b, &pb);
    if (ha && hb) {
        int c = cmp_pos(pa, pb);
        if (c) return c;
    } else if (ha) {
        return -1;
    } else if (hb) {
        return 1;
    }
    return cmp_sv(a_name, b_name);
}

// The symbol tsc's compareTypeNames orders by (getTypeNameSymbol): a name, and the symbol for the identity test
typedef struct TypeName {
    bool named;
    void const* identity;
    csview name;
    Symbol const* symbol; // The identity when it's a symbol
} TypeName;

static TypeName type_name(Ty const* t) {
    if (t->alias) return (TypeName){ true, t->alias, t->alias->name, t->alias };
    switch ((TyKind)t->kind) {
        case TY_TYPE_PARAM:
            if (t->param.symbol) return (TypeName){ true, t->param.symbol, t->param.symbol->name, t->param.symbol };
            return (TypeName){ true, t, atom_sv(t->param.name), nullptr };
        case TY_STRING_MAPPING:
            if (t->string_mapping.symbol) return (TypeName){ true, t->string_mapping.symbol, t->string_mapping.symbol->name, t->string_mapping.symbol };
            return (TypeName){0};
        case TY_DECLARED:
            switch (t->declared.kind) {
                case TY_DECLARED_INTERFACE:
                case TY_DECLARED_CLASS_INSTANCE:
                case TY_DECLARED_SYNTHETIC:
                    if (t->declared.symbol) return (TypeName){ true, t->declared.symbol, escaped_name(t->declared.symbol), t->declared.symbol };
                    return (TypeName){ true, t, atom_sv(t->declared.name), nullptr };
                default:
                    return (TypeName){0};
            }
        case TY_REFERENCE:
            return type_name(t->reference.target);
        default:
            return (TypeName){0};
    }
}

static int cmp_lists(Ty const* const* a, u32 alen, Ty const* const* b, u32 blen) {
    if (alen != blen) return alen < blen ? -1 : 1;
    for (u32 i = 0; i < alen; i++) {
        int c = ty_compare(a[i], b[i]);
        if (c) return c;
    }
    return 0;
}

static int cmp_tylist(TyList a, TyList b) { return cmp_lists(a.data, a.len, b.data, b.len); }

static int cmp_mappers(TyMapper const* a, TyMapper const* b) {
    if (a == b) return 0;
    if (!a) return 1;
    if (!b) return -1;
    int c = cmp_lists(a->sources, a->len, b->sources, b->len);
    return c ? c : cmp_lists(a->targets, a->len, b->targets, b->len);
}

// The symbol of an object type, as tsc's compareSymbols(t.symbol) would place it: a position, or none
typedef struct ObjectSym {
    bool has; // has a symbol at all
    bool positioned;
    OrderPos pos;
    Symbol const* symbol; // If it is a symbol
    void const* identity;
    u32 created; // A symbol made with the type (an instantiation expression's): its id, in creation order like tsc's
} ObjectSym;

static ObjectSym sym_of_symbol(Symbol const* s) {
    ObjectSym r = { .has = s != nullptr, .symbol = s, .identity = s };
    if (s && s->decl_count) r.positioned = span_pos(s->decls[0].full, &r.pos);
    return r;
}

static ObjectSym sym_of_span(Span s, void const* identity) {
    ObjectSym r = { .has = true, .identity = identity };
    r.positioned = span_pos(s, &r.pos);
    return r;
}

static ObjectSym object_symbol(Ty const* t) {
    switch ((TyKind)t->kind) {
        case TY_DECLARED:
            if (t->declared.kind == TY_DECLARED_NAMESPACE && t->declared.namespace_symbol) {
                return sym_of_symbol(t->declared.namespace_symbol);
            }
            return sym_of_symbol(t->declared.symbol);
        case TY_REFERENCE:
            return object_symbol(t->reference.target);
        case TY_ANONYMOUS:
            // (An instantiation expression's type has a symbol of its own without declarations: tsc's
            // getInstantiatedTypePart, `__instantiationExpression`)
            if (t->anonymous.applies_type_args) return (ObjectSym){ .has = true, .identity = t, .created = t->id + 1 };
            if (t->anonymous.target) return object_symbol(t->anonymous.target);
            if (t->anonymous.is_reverse) return (ObjectSym){0};
            if (t->anonymous.merged_value) return sym_of_symbol(t->anonymous.merged_value);
            if (t->anonymous.function) return sym_of_span(t->anonymous.function->full_span, t->anonymous.function);
            // (An overloaded method's type: its symbol, the overloads', by the first declaration, so `find` of
            // Array<never> sorts before ReadonlyArray<W>'s like in tsc)
            if (t->anonymous.overloads && t->anonymous.overload_count) {
                return sym_of_span(t->anonymous.overloads[0]->full_span, t->anonymous.overloads);
            }
            if (t->anonymous.node) {
                TypeNode const* n = t->anonymous.node;
                if (n->tag == TYPE_INTERFACE) return sym_of_span(n->interface.span, n);
                if (n->tag == TYPE_CALLABLE) return sym_of_span(n->callable->function->full_span, n);
                return (ObjectSym){ .has = true, .identity = n };
            }
            if (t->anonymous.object_literal && t->anonymous.literal_expr) {
                return sym_of_span(t->anonymous.literal_expr->inner_span, t->anonymous.literal_expr);
            }
            return (ObjectSym){0};
        default:
            return (ObjectSym){0};
    }
}

static int cmp_object_symbols(ObjectSym a, ObjectSym b) {
    if (a.identity == b.identity && a.has == b.has) return 0;
    if (!a.has) return 1;
    if (!b.has) return -1;
    if (a.positioned && b.positioned) {
        int c = cmp_pos(a.pos, b.pos);
        if (c) return c;
    } else if (a.positioned) {
        return -1;
    } else if (b.positioned) {
        return 1;
    }
    if (a.symbol && b.symbol) return cmp_sv(escaped_name(a.symbol), escaped_name(b.symbol));
    // (Two `__instantiationExpression` symbols: by symbol id)
    if (a.created && b.created) return a.created < b.created ? -1 : 1;
    return 0;
}

// tsc's ObjectFlags.ObjectTypeKindMask bit of a non-reference object type
static u32 object_kind(Ty const* t) {
    switch ((TyKind)t->kind) {
        case TY_DECLARED:
            switch (t->declared.kind) {
                case TY_DECLARED_CLASS_INSTANCE: return 1;
                case TY_DECLARED_INTERFACE:
                case TY_DECLARED_SYNTHETIC: return 2;
                default: return 16;
            }
        case TY_ANONYMOUS:
            if (t->anonymous.is_reverse) return 1024;
            return t->anonymous.mapped ? 32 : 16;
        case TY_EVOLVING_ARRAY: return 256;
        default: return 16;
    }
}

// Whether tsc would see a reference (ObjectFlags.Reference): generic instantiations and tuples
static bool is_reference(Ty const* t) {
    return t->kind == TY_REFERENCE || (t->kind == TY_DECLARED && t->declared.kind == TY_DECLARED_TUPLE_TARGET);
}

static u32 tuple_element_flags(unsigned char f) {
    return f & TUPLE_ELEMENT_REST ? 4 : f & TUPLE_ELEMENT_OPTIONAL ? 2 : 1;
}

static int cmp_tuples(TupleShape const* a, TupleShape const* b) {
    if (a == b) return 0;
    if (a->readonly != b->readonly) return a->readonly ? 1 : -1;
    if (a->arity != b->arity) return a->arity < b->arity ? -1 : 1;
    for (u32 i = 0; i < a->arity; i++) {
        u32 fa = tuple_element_flags(a->flags ? a->flags[i] : 0), fb = tuple_element_flags(b->flags ? b->flags[i] : 0);
        if (fa != fb) return fa < fb ? -1 : 1;
    }
    if (a->names || b->names) {
        for (u32 i = 0; i < a->arity; i++) {
            Atom const* na = a->names ? a->names[i] : nullptr;
            Atom const* nb = b->names ? b->names[i] : nullptr;
            if (na == nb) continue;
            if (!na) return -1;
            if (!nb) return 1;
            int c = cmp_sv(atom_sv(na), atom_sv(nb));
            if (c) return c;
        }
    }
    return 0;
}

static TupleShape const* tuple_of(Ty const* ref) {
    Ty const* target = ref->kind == TY_REFERENCE ? ref->reference.target : ref;
    return target->kind == TY_DECLARED ? target->declared.tuple : nullptr;
}

static u32 origin_rank(Ty const* o) {
    return o->kind == TY_KEYOF ? RANK_INDEX : o->kind == TY_UNION ? RANK_UNION : RANK_INTERSECTION;
}

// A type's sort key: a prefix of what ty_compare_tie() orders by, so that most comparisons are one integer compare.
// Bits 63-59 the rank, bit 58 set if unnamed, 57-1 a 57 bit prefix of the next criterion (0 when there's none cheap:
// then equal keys go to ty_compare_tie()), bit 0 set (computed). Different keys must order like ty_compare_tie()
enum { KEY_RANK_SHIFT = 59, KEY_UNNAMED = 1ull << 58, KEY_MASK = (1ull << 57) - 1 };

// The first 7 bytes, big-endian (memcmp order, a shorter string first as its padding is 0)
static u64 sv_prefix(csview sv) {
    u64 r = 0;
    for (isize i = 0; i < 7; i++) r = r << 8 | (i < sv.size ? (unsigned char)sv.buf[i] : 0);
    return r;
}

// cmp_object_symbols() order: positioned (by file, then offset), then with a symbol but no position, then none
static u64 object_prefix(ObjectSym s) {
    if (!s.has) return 2ull << 55;
    if (!s.positioned) return 1ull << 55;
    return 0;
}

static u64 compute_key(Ty const* t) {
    u64 rank = ty_order_rank(t);
    u64 key = rank << KEY_RANK_SHIFT | 1;
    TypeName n = type_name(t);
    u64 prefix = 0;
    if (n.named) {
        prefix = sv_prefix(n.name);
    } else {
        key |= KEY_UNNAMED;
        switch ((TyKind)t->kind) {
            case TY_LITERAL:
                if (rank == RANK_ENUM) {
                    ObjectSym s = sym_of_symbol(t->literal.enum_member);
                    prefix = object_prefix(s);
                    if (s.has && s.positioned && s.pos.file < (1u << 23)) prefix = (u64)s.pos.file << 32 | s.pos.offset;
                } else if (rank == RANK_STRING_LITERAL) {
                    prefix = sv_prefix(atom_sv(t->literal.string));
                } else if (rank == RANK_NUMBER_LITERAL) {
                    double x = t->literal.value.number;
                    if (x == 0) x = 0; // (-0 == 0)
                    u64 bits;
                    memcpy(&bits, &x, sizeof bits);
                    bits = bits >> 63 ? ~bits : bits | 1ull << 63;
                    prefix = bits >> 8;
                } else if (rank == RANK_BOOLEAN_LITERAL) {
                    prefix = t->literal.value.boolean;
                }
                break;
            case TY_DECLARED:
            case TY_REFERENCE:
            case TY_ANONYMOUS:
            case TY_EVOLVING_ARRAY: {
                ObjectSym s = object_symbol(t);
                prefix = object_prefix(s);
                if (s.has && s.positioned && s.pos.file < (1u << 23)) prefix = (u64)s.pos.file << 32 | s.pos.offset;
                break;
            }
            default:
                break;
        }
    }
    return key | (prefix & KEY_MASK) << 1;
}

u64 ty_order_key(Ty const* t) {
    u64 k = t->order_key;
    if (k) return k;
    k = compute_key(t);
    ((Ty*)t)->order_key = k; // (A cache: the checker that owns the type is the only one reading it)
    return k;
}

// By value (tsgo's compareTypes: PseudoBigInt.Compare), as decimal digits
static int cmp_bigints(BigintValue a, BigintValue b) {
    cstr x = cstr_init(), y = cstr_init();
    bigint_append_decimal(&x, a);
    bigint_append_decimal(&y, b);
    csview xs = cstr_sv(&x), ys = cstr_sv(&y);
    bool xn = xs.size && xs.buf[0] == '-', yn = ys.size && ys.buf[0] == '-';
    int c = 0;
    if (xn != yn) {
        c = xn ? -1 : 1;
    } else {
        // (Same sign: more digits is further from zero, else the digits decide)
        c = xs.size != ys.size ? (xs.size < ys.size ? -1 : 1) : cmp_sv(xs, ys);
        if (xn) c = -c;
    }
    cstr_drop(&x);
    cstr_drop(&y);
    return c;
}

// The whole of compareTypes, for types whose keys are equal (same rank)
int ty_compare_tie(Ty const* a, Ty const* b, u64 key) {
    u32 ra = (u32)(key >> KEY_RANK_SHIFT);
    // tsc's compareTypeNames (equal keys: both named or both not)
    if (!(key & KEY_UNNAMED)) {
        TypeName na = type_name(a), nb = type_name(b);
        if (na.identity == nb.identity) {
            if (a->alias && a->alias == b->alias) {
                int c = cmp_tylist(a->alias_args, b->alias_args);
                if (c) return c;
            }
        } else {
            int c = cmp_sv(na.name, nb.name);
            if (c) return c;
            // (Distinct same-named declarations stay together, by declaration: tsgo 7.1's compareTypeNames)
            if (na.symbol && nb.symbol) {
                c = ty_compare_symbols(na.symbol, nb.symbol);
                if (c) return c;
            }
        }
    } else if (ra == RANK_STRING_LITERAL) {
        // (The common tie: a long common prefix)
        int c = a->literal.string == b->literal.string ? 0 : cmp_sv(atom_sv(a->literal.string), atom_sv(b->literal.string));
        if (c) return c;
        // (A literal and its fresh twin: tsc makes the fresh one from the regular one, a later id)
        if ((a->flags ^ b->flags) & TYF_FRESH_LITERAL) return a->flags & TYF_FRESH_LITERAL ? 1 : -1;
        return a->id < b->id ? -1 : 1;
    }

    switch ((TyKind)a->kind) {
        case TY_INTRINSIC:
            break;
        case TY_LITERAL: {
            if (ra == RANK_ENUM) {
                int c = ty_compare_symbols(a->literal.enum_member, b->literal.enum_member);
                if (c) return c;
            } else if (ra == RANK_UNIQUE_ES_SYMBOL) {
                UniqueSymbolDecl const* ua = a->literal.unique;
                UniqueSymbolDecl const* ub = b->literal.unique;
                if (ua && ub) {
                    OrderPos pa = {0}, pb = {0};
                    bool ha = ua->member.buf ? span_pos(ua->member, &pa) : (ua->owner && ua->owner->decl_count && span_pos(ua->owner->decls[0].full, &pa));
                    bool hb = ub->member.buf ? span_pos(ub->member, &pb) : (ub->owner && ub->owner->decl_count && span_pos(ub->owner->decls[0].full, &pb));
                    // (tsc's compareSymbols: a declaration first, then by name)
                    if (ha && hb) {
                        int c = cmp_pos(pa, pb);
                        if (c) return c;
                    } else if (ha) {
                        return -1;
                    } else if (hb) {
                        return 1;
                    } else if (ua->owner && ub->owner) {
                        int c = cmp_sv(ua->owner->name, ub->owner->name);
                        if (c) return c;
                    }
                }
            } else if (ra == RANK_STRING_LITERAL) {
                int c = cmp_sv(atom_sv(a->literal.string), atom_sv(b->literal.string));
                if (c) return c;
            } else if (ra == RANK_NUMBER_LITERAL) {
                double x = a->literal.value.number, y = b->literal.value.number;
                if (x != y) return x < y ? -1 : 1;
            } else if (ra == RANK_BIGINT_LITERAL) {
                int c = cmp_bigints(a->literal.value.bigint, b->literal.value.bigint);
                if (c) return c;
            } else if (ra == RANK_BOOLEAN_LITERAL) {
                bool x = a->literal.value.boolean, y = b->literal.value.boolean;
                if (x != y) return x ? 1 : -1;
            }
            // (Equal values: a literal and its fresh twin, which tsc makes from the regular one, a later id)
            if (ra != RANK_BIGINT_LITERAL && ra != RANK_UNIQUE_ES_SYMBOL && ((a->flags ^ b->flags) & TYF_FRESH_LITERAL)) {
                return a->flags & TYF_FRESH_LITERAL ? 1 : -1;
            }
            break;
        }
        case TY_DECLARED:
        case TY_REFERENCE:
        case TY_ANONYMOUS:
        case TY_EVOLVING_ARRAY: {
            int c = cmp_object_symbols(object_symbol(a), object_symbol(b));
            if (c) return c;
            bool ia = is_reference(a), ib = is_reference(b);
            if (ia && ib) {
                TupleShape const* ta = tuple_of(a);
                TupleShape const* tb = tuple_of(b);
                if (ta && tb) {
                    c = cmp_tuples(ta, tb);
                    if (c) return c;
                }
                if (a->kind != TY_REFERENCE || b->kind != TY_REFERENCE) {
                    // (A tuple target without elements: no arguments)
                    if (a->kind != b->kind) return a->kind == TY_REFERENCE ? 1 : -1;
                } else if (!a->reference.deferred && !b->reference.deferred) {
                    c = cmp_tylist(a->reference.args, b->reference.args);
                    if (c) return c;
                    if (a->reference.this_arg != b->reference.this_arg) {
                        if (!a->reference.this_arg) return -1;
                        if (!b->reference.this_arg) return 1;
                        c = ty_compare(a->reference.this_arg, b->reference.this_arg);
                        if (c) return c;
                    }
                } else if (a->reference.deferred && b->reference.deferred) {
                    // (By the reference node's position first: tsc's compareNodes)
                    TypeNode const* na = a->reference.deferred->node;
                    TypeNode const* nb = b->reference.deferred->node;
                    if (na != nb) {
                        Span sa = span_null, sb = span_null;
                        bool ha = type_node_start(na, &sa), hb = type_node_start(nb, &sb);
                        c = cmp_node_spans(sa, sb, ha, hb);
                        if (c) return c;
                    }
                    c = cmp_mappers(a->reference.deferred->mapper, b->reference.deferred->mapper);
                    if (c) return c;
                }
            } else if (ia) {
                return -1;
            } else if (ib) {
                return 1;
            } else {
                u32 ka = object_kind(a), kb = object_kind(b);
                if (ka != kb) return ka < kb ? -1 : 1;
                // (Reverse mapped types by their source, then their constraint: tsgo's compareTypes)
                ReverseMapped const* rva = ty_reverse_of(a);
                ReverseMapped const* rvb = ty_reverse_of(b);
                if (rva && rvb) {
                    c = ty_compare(rva->source, rvb->source);
                    if (c) return c;
                    // (The mapped types: by their type parameter, which is where they are declared)
                    if (rva->mapped != rvb->mapped && rva->mapped->type_param && rvb->mapped->type_param) {
                        c = ty_compare(rva->mapped->type_param, rvb->mapped->type_param);
                        if (c) return c;
                    }
                    if (rva->operand != rvb->operand && rva->operand && rvb->operand) {
                        c = ty_compare(rva->operand, rvb->operand);
                        if (c) return c;
                    }
                }
                if (a->kind == TY_ANONYMOUS && b->kind == TY_ANONYMOUS && !rva && !rvb) {
                    c = cmp_mappers(a->anonymous.mapper, b->anonymous.mapper);
                    if (c) return c;
                }
            }
            break;
        }
        case TY_UNION: {
            Ty const* oa = a->composite.origin;
            Ty const* ob = b->composite.origin;
            if (!oa && !ob) {
                int c = cmp_tylist(a->composite.types, b->composite.types);
                if (c) return c;
            } else if (!oa) {
                return 1;
            } else if (!ob) {
                return -1;
            } else {
                // (A keyof origin isn't a Ty that ty_compare() knows the identity of, compare its operand)
                if (oa->kind != ob->kind) return origin_rank(oa) < origin_rank(ob) ? -1 : 1;
                int c = oa->kind == TY_KEYOF ? ty_compare(oa->keyof.operand, ob->keyof.operand) :
                    cmp_tylist(oa->composite.types, ob->composite.types);
                if (c) return c;
            }
            break;
        }
        case TY_INTERSECTION: {
            int c = cmp_tylist(a->composite.types, b->composite.types);
            if (c) return c;
            break;
        }
        case TY_TYPE_PARAM: {
            int c = ty_compare_symbols(a->param.symbol, b->param.symbol);
            if (c) return c;
            break;
        }
        case TY_KEYOF: {
            int c = ty_compare(a->keyof.operand, b->keyof.operand);
            if (c) return c;
            break;
        }
        case TY_INDEXED: {
            int c = ty_compare(a->indexed.object, b->indexed.object);
            if (c) return c;
            c = ty_compare(a->indexed.index, b->indexed.index);
            if (c) return c;
            break;
        }
        case TY_CONDITIONAL: {
            // By the root's node position (tsc's compareNodes(root.node)); roots not lowered from a node by id
            Ty const* xa = a->conditional.root;
            Ty const* xb = b->conditional.root;
            if (xa != xb) {
                TyConditionalOuter const* oa = xa->conditional.outer;
                TyConditionalOuter const* ob = xb->conditional.outer;
                int c = cmp_node_spans(oa ? oa->node : span_null, ob ? ob->node : span_null, oa != nullptr, ob != nullptr);
                if (c) return c;
                if (!oa || !ob || oa->node.buf != ob->node.buf) return xa->id < xb->id ? -1 : 1;
            }
            int c = cmp_mappers(a->conditional.mapper, b->conditional.mapper);
            if (c) return c;
            break;
        }
        case TY_SUBSTITUTION: {
            int c = ty_compare(a->substitution.base, b->substitution.base);
            if (c) return c;
            c = ty_compare(a->substitution.constraint, b->substitution.constraint);
            if (c) return c;
            break;
        }
        case TY_TEMPLATE_LITERAL: {
            u32 la = a->template_literal.types.len + 1, lb = b->template_literal.types.len + 1;
            for (u32 i = 0; i < la; i++) {
                if (i >= lb) return 1;
                int c = cmp_sv(atom_sv(a->template_literal.texts[i]), atom_sv(b->template_literal.texts[i]));
                if (c) return c;
            }
            if (la < lb) return -1;
            int c = cmp_tylist(a->template_literal.types, b->template_literal.types);
            if (c) return c;
            break;
        }
        case TY_STRING_MAPPING: {
            int c = ty_compare(a->string_mapping.type, b->string_mapping.type);
            if (c) return c;
            break;
        }
    }
    return a->id < b->id ? -1 : 1;
}
