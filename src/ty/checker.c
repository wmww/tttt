#include "ty/ty_internal.h"
#include <math.h>
#include "declaration.h"
#include "program.h"
#include "scope.h"

// Arena

#define ARENA_CHUNK_SIZE (64 * 1024)

struct ArenaChunk {
    ArenaChunk* next;
    size_t used;
    size_t cap;
    char data[];
};

void* arena_alloc(Arena* me, size_t size) {
    size = (size + 7) & ~(size_t)7;
    my total += size;
    if (!my head || my head->used + size > my head->cap) {
        size_t cap = size > ARENA_CHUNK_SIZE ? size : ARENA_CHUNK_SIZE;
        ArenaChunk* chunk = calloc(1, sizeof(ArenaChunk) + cap);
        panic_if(!chunk, "out of memory");
        chunk->cap = cap;
        if (my head && size > ARENA_CHUNK_SIZE) {
            // A big one-off allocation, keep filling the current chunk
            chunk->next = my head->next;
            my head->next = chunk;
        } else {
            chunk->next = my head;
            my head = chunk;
        }
        chunk->used = size;
        return chunk->data;
    }
    void* ret = my head->data + my head->used;
    my head->used += size;
    return ret;
}

void arena_drop(Arena* me) {
    ArenaChunk* i = my head;
    while (i) {
        ArenaChunk* next = i->next;
        free(i);
        i = next;
    }
    *me = (Arena){0};
}

// Intern table

static u64 hash_words(u64 const* words, u32 count) {
    u64 h = 0x9e3779b97f4a7c15ull ^ count;
    for (u32 i = 0; i < count; i++) {
        h ^= words[i];
        h *= 0xff51afd7ed558ccdull;
        h ^= h >> 29;
    }
    return h ? h : 1;
}

static InternEntry* intern_find(InternTable const* me, u64 hash, u64 const* words, u32 count) {
    if (!my cap) return nullptr;
    u32 mask = my cap - 1;
    for (u32 i = (u32)hash & mask;; i = (i + 1) & mask) {
        InternEntry* entry = &my entries[i];
        if (!entry->hash) return entry;
        if (entry->hash == hash && entry->key[0] == count &&
            memcmp(entry->key + 1, words, sizeof(u64) * count) == 0
        ) {
            return entry;
        }
    }
}

static void intern_grow(InternTable* me) {
    InternTable bigger = { .cap = my cap ? my cap * 2 : 64, .len = my len };
    bigger.entries = calloc(bigger.cap, sizeof(InternEntry));
    panic_if(!bigger.entries, "out of memory");
    for (u32 i = 0; i < my cap; i++) {
        InternEntry* entry = &my entries[i];
        if (entry->hash) {
            *intern_find(&bigger, entry->hash, entry->key + 1, (u32)entry->key[0]) = *entry;
        }
    }
    free(my entries);
    *me = bigger;
}

// The returned slot is null if the key is new. It's only valid until the next intern_slot() call on the same table,
// so anything that interns while computing the value has to look the slot up again afterwards.
void const** intern_slot(Checker* me, InternTable* table, u64 const* words, u32 count) {
    if ((table->len + 1) * 4 > table->cap * 3) {
        intern_grow(table);
    }
    u64 hash = hash_words(words, count);
    InternEntry* entry = intern_find(table, hash, words, count);
    if (!entry->hash) {
        u64* key = arena_new_n(&my arena, u64, count + 1);
        key[0] = count;
        memcpy(key + 1, words, sizeof(u64) * count);
        entry->hash = hash;
        entry->key = key;
        table->len++;
    }
    return &entry->value;
}

void const* intern_get(InternTable const* table, u64 const* words, u32 count) {
    InternEntry* entry = intern_find(table, hash_words(words, count), words, count);
    return entry ? entry->value : nullptr;
}

Atom const* checker_atom(Checker* me, csview sv) {
    // Atoms are keyed by content, so they get their own probing rather than the word keys
    InternTable* table = &my atoms;
    if ((table->len + 1) * 4 > table->cap * 3) {
        InternTable bigger = { .cap = table->cap ? table->cap * 2 : 256, .len = table->len };
        bigger.entries = calloc(bigger.cap, sizeof(InternEntry));
        panic_if(!bigger.entries, "out of memory");
        for (u32 i = 0; i < table->cap; i++) {
            InternEntry* entry = &table->entries[i];
            if (!entry->hash) continue;
            u32 j = (u32)entry->hash & (bigger.cap - 1);
            while (bigger.entries[j].hash) j = (j + 1) & (bigger.cap - 1);
            bigger.entries[j] = *entry;
        }
        free(table->entries);
        *table = bigger;
    }
    // c_hash_n() leaves similar strings (`ns1.Foo`, `ns2.Foo`) close together in the low bits, which linear probing
    // turns into long runs: finish it off (murmur3's finalizer)
    u64 hash = c_hash_n(sv.buf, sv.size);
    hash ^= hash >> 33;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33;
    hash *= 0xc4ceb9fe1a85ec53ULL;
    hash ^= hash >> 33;
    if (!hash) hash = 1;
    u32 mask = table->cap - 1;
    for (u32 i = (u32)hash & mask;; i = (i + 1) & mask) {
        InternEntry* entry = &table->entries[i];
        if (!entry->hash) {
            Atom* atom = arena_alloc(&my arena, sizeof(Atom) + (size_t)sv.size + 1);
            atom->len = (u32)sv.size;
            memcpy(atom->str, sv.buf, (size_t)sv.size);
            entry->hash = hash;
            entry->value = atom;
            table->len++;
            return atom;
        }
        Atom const* atom = entry->value;
        if (entry->hash == hash && atom->len == (u32)sv.size && memcmp(atom->str, sv.buf, (size_t)sv.size) == 0) {
            return atom;
        }
    }
}

// Types

Ty* ty_alloc(Checker* me, TyKind kind, u32 flags) {
    Ty* ret = arena_new(&my arena, Ty);
    ret->id = my next_type_id++;
    ret->kind = kind;
    ret->flags = flags;
    return ret;
}

TyList ty_list_copy(Checker* me, Ty const* const* data, u32 len) {
    if (!len) return (TyList){0};
    Ty const** copy = arena_new_n(&my arena, Ty const*, len);
    memcpy(copy, data, sizeof(Ty const*) * len);
    return (TyList){ .data = copy, .len = len };
}

static u32 ty_list_flags(TyList list) {
    u32 flags = 0;
    for (u32 i = 0; i < list.len; i++) flags |= list.data[i]->flags;
    return flags;
}

static Ty const* make_intrinsic(Checker* me, char const* name, u32 flags) {
    Ty* ret = ty_alloc(me, TY_INTRINSIC, flags);
    ret->intrinsic.name = csview_from(name);
    return ret;
}

Checker* checker_new(Program* program) {
    Checker* me = calloc(1, sizeof(Checker));
    panic_if(!me, "out of memory");
    my program = program;
    my next_type_id = 1;
    my intrinsics[TY_ANY] = make_intrinsic(me, "any", TYF_ANY);
    my intrinsics[TY_ERROR] = make_intrinsic(me, "any", TYF_ANY | TYF_ERROR);
    my intrinsics[TY_UNKNOWN_] = make_intrinsic(me, "unknown", TYF_UNKNOWN);
    my intrinsics[TY_UNDEFINED] = make_intrinsic(me, "undefined", TYF_UNDEFINED);
    my intrinsics[TY_NULL] = make_intrinsic(me, "null", TYF_NULL);
    my intrinsics[TY_STRING] = make_intrinsic(me, "string", TYF_STRING);
    my intrinsics[TY_NUMBER] = make_intrinsic(me, "number", TYF_NUMBER);
    my intrinsics[TY_BIGINT] = make_intrinsic(me, "bigint", TYF_BIGINT);
    Value false_value = { .tag = VALUE_BOOLEAN, .boolean = false };
    Value true_value = { .tag = VALUE_BOOLEAN, .boolean = true };
    my intrinsics[TY_FALSE] = ty_literal(me, &false_value);
    my intrinsics[TY_TRUE] = ty_literal(me, &true_value);
    my intrinsics[TY_BOOLEAN] = ty_union(me, &my intrinsics[TY_FALSE], 2, nullptr, (TyList){0});
    my intrinsics[TY_ES_SYMBOL] = make_intrinsic(me, "symbol", TYF_ES_SYMBOL);
    my intrinsics[TY_VOID] = make_intrinsic(me, "void", TYF_VOID);
    my intrinsics[TY_NEVER] = make_intrinsic(me, "never", TYF_NEVER);
    my intrinsics[TY_SILENT_NEVER] = make_intrinsic(me, "never", TYF_NEVER);
    my intrinsics[TY_NON_PRIMITIVE] = make_intrinsic(me, "object", TYF_NON_PRIMITIVE);
    return me;
}

thread_local Checker* current_checker;

void checker_make_current(Checker* me) {
    current_checker = me;
    current_lazy_slots = me ? &my lazy : nullptr;
}

void checker_drop(Checker* me) {
    if (!me) return;
    // Not current while dropping: memos contain LazyTypes of their own, which mustn't look for slots in here anymore
    if (current_checker == me) checker_make_current(nullptr);
    lazy_slots_drop(&my lazy);
    free(my atoms.entries);
    free(my types.entries);
    free(my links.entries);
    for (u32 i = 0; i < TY_RELATION_COUNT; i++) relation_cache_drop(&my relations[i]);
    arena_drop(&my arena);
    free(me);
}

static Ty const* literal_type(Checker* me, Value const* value, Declaration const* enum_decl, Declaration const* enum_member) {
    // An enum's literal types are per (value, enum): members of one enum with the same value share a type, as in tsc
    u64 key[4] = { KEY_LITERAL, value->tag, 0, (u64)(uintptr_t)enum_decl };
    Atom const* string = nullptr;
    u32 flags = 0;
    switch (value->tag) {
        case VALUE_UNDEFINED: return my intrinsics[TY_UNDEFINED];
        case VALUE_NULL: return my intrinsics[TY_NULL];
        case VALUE_BOOLEAN: key[2] = value->boolean; flags = TYF_BOOLEAN_LITERAL; break;
        case VALUE_NUMBER: memcpy(&key[2], &value->number, sizeof(double)); flags = TYF_NUMBER_LITERAL; break;
        case VALUE_BIGINT: memcpy(&key[2], &value->bigint, sizeof(double)); flags = TYF_BIGINT_LITERAL; break;
        case VALUE_STRING:
            string = checker_atom(me, cstr_sv(&value->string));
            key[2] = (u64)(uintptr_t)string;
            flags = TYF_STRING_LITERAL;
            break;
        default:
            // Objects, arrays, functions and symbols aren't literal types; the legacy checker shouldn't produce them
            return my intrinsics[TY_ERROR];
    }
    if (enum_decl) flags |= TYF_ENUM_LITERAL;
    void const** slot = intern_slot(me, &my types, key, 4);
    if (!*slot) {
        Ty* ret = ty_alloc(me, TY_LITERAL, flags);
        ret->literal.value = *value;
        if (string) {
            // Borrow the atom's storage, so the literal doesn't depend on the lifetime of the Value it came from
            ret->literal.value.string = cstr_null;
        }
        ret->literal.string = string;
        ret->literal.enum_decl = enum_decl;
        ret->literal.enum_member = enum_member;
        *slot = ret;
    }
    return *slot;
}

// The type of a type name that didn't resolve: the error type, but it prints as what was written, like in tsc
Ty const* ty_unresolved(Checker* me, csview name) {
    Atom const* atom = checker_atom(me, name);
    u64 key[3] = { KEY_LITERAL, UINT32_MAX - 1, (u64)(uintptr_t)atom };
    void const** slot = intern_slot(me, &my types, key, 3);
    if (!*slot) {
        Ty* ret = ty_alloc(me, TY_INTRINSIC, TYF_ANY | TYF_ERROR);
        ret->intrinsic.name = atom_sv(atom);
        *slot = ret;
    }
    return *slot;
}

Ty const* ty_literal(Checker* me, Value const* value) {
    return literal_type(me, value, nullptr, nullptr);
}

Ty const* ty_enum_literal(Checker* me, Value const* value, Declaration const* enum_decl, Declaration const* member) {
    return literal_type(me, value, enum_decl, member);
}

// The type of an enum member whose value isn't a compile time constant (tsc's computed enum type, TypeFlags.Enum).
// Also the type of an enum without members, with the enum as its own member
Ty const* ty_enum_computed_member(Checker* me, Declaration const* enum_decl, Declaration const* member) {
    u64 key[4] = { KEY_LITERAL, UINT32_MAX, (u64)(uintptr_t)member, (u64)(uintptr_t)enum_decl };
    void const** slot = intern_slot(me, &my types, key, 4);
    if (!*slot) {
        Ty* ret = ty_alloc(me, TY_LITERAL, TYF_ENUM_LITERAL | TYF_ENUM);
        ret->literal.value = (Value){ .tag = VALUE_NUMBER, .number = NAN };
        ret->literal.enum_decl = enum_decl;
        ret->literal.enum_member = member;
        *slot = ret;
    }
    return *slot;
}

Ty const* ty_string_literal(Checker* me, csview sv) {
    Value value = { .tag = VALUE_STRING, .string = cstr_from_sv(sv) };
    Ty const* ret = ty_literal(me, &value);
    value_drop(&value);
    return ret;
}

// Unions and intersections

static int ty_cmp_id(void const* a, void const* b) {
    u32 a_id = (*(Ty const* const*)a)->id;
    u32 b_id = (*(Ty const* const*)b)->id;
    return a_id < b_id ? -1 : a_id > b_id;
}

static void ty_buf_add_flattened(TyBuf* me, TyKind kind, Ty const* const* types, u32 len) {
    for (u32 i = 0; i < len; i++) {
        if (types[i]->kind == kind) {
            ty_buf_add_flattened(me, kind, types[i]->composite.types.data, types[i]->composite.types.len);
        } else {
            ty_buf_push(me, types[i]);
        }
    }
}

static Ty const* intern_composite(
    Checker* me, TyKind kind, Ty const* const* types, u32 len, Declaration const* alias, TyList alias_args
) {
    u32 count = 3 + alias_args.len + len;
    u64 stack_key[64];
    u64* key = count <= c_arraylen(stack_key) ? stack_key : malloc(sizeof(u64) * count);
    key[0] = kind == TY_UNION ? KEY_UNION : KEY_INTERSECTION;
    key[1] = (u64)(uintptr_t)alias;
    key[2] = alias_args.len;
    for (u32 i = 0; i < alias_args.len; i++) key[3 + i] = alias_args.data[i]->id;
    for (u32 i = 0; i < len; i++) key[3 + alias_args.len + i] = types[i]->id;
    void const** slot = intern_slot(me, &my types, key, count);
    if (key != stack_key) free(key);
    if (!*slot) {
        u32 flags = 0;
        for (u32 i = 0; i < len; i++) flags |= types[i]->flags;
        flags = (flags | ty_list_flags(alias_args)) & TYF_COULD_CONTAIN_TYPE_VARIABLES;
        flags |= kind == TY_UNION ? TYF_UNION : TYF_INTERSECTION;
        if (kind == TY_UNION && len == 2 && types[0] == my intrinsics[TY_FALSE] && types[1] == my intrinsics[TY_TRUE]) {
            flags |= TYF_BOOLEAN;
        }
        Ty* ret = ty_alloc(me, kind, flags);
        ret->alias = alias;
        ret->alias_args = ty_list_copy(me, alias_args.data, alias_args.len);
        ret->composite.types = ty_list_copy(me, types, len);
        *slot = ret;
    }
    return *slot;
}

// Only the cheap reductions: flatten, dedupe, absorb any/unknown/never, drop literals next to their primitive.
// Subtype reduction is a separate operation (TODO, the legacy type_composite() does its own through the relation).
Ty const* ty_union(Checker* me, Ty const* const* types, u32 len, Declaration const* alias, TyList alias_args) {
    if (len == 1) return types[0];
    TyBuf buf = {0};
    ty_buf_add_flattened(&buf, TY_UNION, types, len);

    u32 includes = 0;
    bool includes_error = false;
    for (u32 i = 0; i < buf.len; i++) {
        includes |= buf.data[i]->flags;
        includes_error |= buf.data[i]->kind == TY_INTRINSIC && (buf.data[i]->flags & TYF_ERROR);
    }
    Ty const* ret = nullptr;
    if (includes & TYF_ANY) {
        ret = my intrinsics[includes_error ? TY_ERROR : TY_ANY];
    } else if (includes & TYF_UNKNOWN) {
        ret = my intrinsics[TY_UNKNOWN_];
    } else {
        qsort(buf.data, buf.len, sizeof(Ty const*), ty_cmp_id);
        u32 out = 0;
        for (u32 i = 0; i < buf.len; i++) {
            Ty const* t = buf.data[i];
            if (out && buf.data[out - 1] == t) continue;
            if (t->kind == TY_INTRINSIC && (t->flags & TYF_NEVER)) continue;
            if (t->kind == TY_LITERAL && (
                ((t->flags & TYF_STRING_LITERAL) && (includes & TYF_STRING)) ||
                ((t->flags & TYF_NUMBER_LITERAL) && (includes & TYF_NUMBER)) ||
                ((t->flags & TYF_BIGINT_LITERAL) && (includes & TYF_BIGINT))
            )) continue;
            buf.data[out++] = t;
        }
        if (out == 0) {
            ret = my intrinsics[TY_NEVER];
        } else if (out == 1) {
            ret = buf.data[0];
        } else {
            ret = intern_composite(me, TY_UNION, buf.data, out, alias, alias_args);
        }
    }
    ty_buf_drop(&buf);
    return ret;
}

// Which disjoint family of primitives a type belongs to, 0 if none
static u32 primitive_family(Ty const* t) {
    if (t->kind != TY_INTRINSIC && t->kind != TY_LITERAL) return 0;
    if (t->flags & (TYF_STRING | TYF_STRING_LITERAL)) return TYF_STRING;
    if (t->flags & (TYF_NUMBER | TYF_NUMBER_LITERAL)) return TYF_NUMBER;
    if (t->flags & (TYF_BIGINT | TYF_BIGINT_LITERAL)) return TYF_BIGINT;
    if (t->flags & TYF_BOOLEAN_LITERAL) return TYF_BOOLEAN_LITERAL;
    return t->flags & (TYF_ES_SYMBOL | TYF_VOID | TYF_UNDEFINED | TYF_NULL);
}

static bool is_primitive_union(Ty const* t) {
    if (t->kind != TY_UNION) return false;
    for (u32 i = 0; i < t->composite.types.len; i++) {
        if (!primitive_family(t->composite.types.data[i])) return false;
    }
    return true;
}

static bool union_contains(Ty const* u, Ty const* t) {
    for (u32 i = 0; i < u->composite.types.len; i++) {
        if (u->composite.types.data[i] == t) return true;
    }
    return false;
}

// `("a" | "b") & ("b" | "c")` is `"b"`: unions of primitives are intersected as sets rather than distributed over, which
// would be a cross product (`keyof (A | B | C | ...)` is such an intersection). All but the first of them leave `buf`,
// the first is replaced by the result
static void intersect_unions_of_primitives(Checker* me, TyBuf* buf) {
    u32 first = buf->len;
    u32 count = 0;
    for (u32 i = 0; i < buf->len; i++) {
        if (!is_primitive_union(buf->data[i])) continue;
        if (!count++) first = i;
    }
    if (count < 2) return;

    Ty const** unions = malloc(sizeof(Ty const*) * count);
    u32 out = 0;
    count = 0;
    for (u32 i = 0; i < buf->len; i++) {
        if (is_primitive_union(buf->data[i])) {
            unions[count++] = buf->data[i];
            if (i != first) continue;
        }
        buf->data[out++] = buf->data[i];
    }
    buf->len = out;

    TyBuf result = {0};
    for (u32 u = 0; u < count; u++) {
        TyList members = unions[u]->composite.types;
        for (u32 i = 0; i < members.len; i++) {
            Ty const* t = members.data[i];
            bool everywhere = true;
            for (u32 other = 0; other < count && everywhere; other++) {
                if (union_contains(unions[other], t)) continue;
                // A literal is also matched by its primitive: `("a" | 1) & (string | 2)` keeps "a"
                u32 family = t->kind == TY_LITERAL ? primitive_family(t) : 0;
                Ty const* primitive =
                    family == TYF_STRING ? my intrinsics[TY_STRING] :
                    family == TYF_NUMBER ? my intrinsics[TY_NUMBER] :
                    family == TYF_BIGINT ? my intrinsics[TY_BIGINT] : nullptr;
                everywhere = primitive && union_contains(unions[other], primitive);
            }
            if (everywhere) ty_buf_push(&result, t);
        }
    }
    buf->data[first] = ty_union(me, result.data, result.len, nullptr, (TyList){0});
    ty_buf_drop(&result);
    free(unions);
}

Ty const* ty_intersection(Checker* me, Ty const* const* types, u32 len, Declaration const* alias, TyList alias_args) {
    if (len == 1) return types[0];
    TyBuf buf = {0};
    ty_buf_add_flattened(&buf, TY_INTERSECTION, types, len);
    intersect_unions_of_primitives(me, &buf);
    if (buf.len == 1) {
        Ty const* only = buf.data[0];
        ty_buf_drop(&buf);
        return only;
    }

    u32 includes = 0;
    bool includes_error = false;
    Ty const* first_union = nullptr;
    for (u32 i = 0; i < buf.len; i++) {
        Ty const* t = buf.data[i];
        includes |= t->flags;
        includes_error |= t->kind == TY_INTRINSIC && (t->flags & TYF_ERROR);
        if (t->kind == TY_UNION && !first_union) first_union = t;
    }

    Ty const* ret = nullptr;
    if (includes & TYF_NEVER) {
        ret = my intrinsics[TY_NEVER];
    } else if (includes & TYF_ANY) {
        ret = my intrinsics[includes_error ? TY_ERROR : TY_ANY];
    } else if (first_union) {
        // A & (B | C) is (A & B) | (A & C)
        TyBuf results = {0};
        for (u32 i = 0; i < first_union->composite.types.len; i++) {
            TyBuf parts = {0};
            for (u32 j = 0; j < buf.len; j++) {
                ty_buf_push(&parts, buf.data[j] == first_union ? first_union->composite.types.data[i] : buf.data[j]);
            }
            ty_buf_push(&results, ty_intersection(me, parts.data, parts.len, nullptr, (TyList){0}));
            ty_buf_drop(&parts);
        }
        ret = ty_union(me, results.data, results.len, alias, alias_args);
        ty_buf_drop(&results);
    } else {
        // Insertion order is kept (it's observable in how tsc prints), duplicates and `unknown` go
        u32 out = 0;
        u32 family = 0;
        Ty const* literal = nullptr;
        bool is_never = false;
        for (u32 i = 0; i < buf.len && !is_never; i++) {
            Ty const* t = buf.data[i];
            if (t->flags & TYF_UNKNOWN) continue;
            bool duplicate = false;
            for (u32 j = 0; j < out; j++) duplicate |= buf.data[j] == t;
            if (duplicate) continue;
            u32 t_family = primitive_family(t);
            if (t_family) {
                if (family && family != t_family) is_never = true;
                family = t_family;
                if (t->kind == TY_LITERAL) {
                    if (literal && literal != t) is_never = true;
                    literal = t;
                }
            }
            buf.data[out++] = t;
        }
        if (literal) {
            // "a" & string is "a"
            u32 kept = 0;
            for (u32 i = 0; i < out; i++) {
                if (buf.data[i]->kind == TY_INTRINSIC && primitive_family(buf.data[i])) continue;
                buf.data[kept++] = buf.data[i];
            }
            out = kept;
        }
        if (is_never) {
            ret = my intrinsics[TY_NEVER];
        } else if (out == 0) {
            ret = my intrinsics[TY_UNKNOWN_];
        } else if (out == 1) {
            ret = buf.data[0];
        } else {
            ret = intern_composite(me, TY_INTERSECTION, buf.data, out, alias, alias_args);
        }
    }
    ty_buf_drop(&buf);
    return ret;
}

// Type parameters

static Ty const* make_type_param(Checker* me, Declaration const* decl, Atom const* name) {
    Ty* ret = ty_alloc(me, TY_TYPE_PARAM, TYF_TYPE_PARAM | TYF_COULD_CONTAIN_TYPE_VARIABLES);
    ret->param.decl = decl;
    ret->param.name = name;
    ret->param.state = arena_new(&my arena, TyParamState);
    return ret;
}

Ty const* ty_type_param(Checker* me, Declaration const* decl) {
    u64 key[2] = { KEY_TYPE_PARAM, (u64)(uintptr_t)decl };
    void const** slot = intern_slot(me, &my links, key, 2);
    if (!*slot) {
        Ty const* ret = make_type_param(me, decl, checker_atom(me, decl->name.sv));
        // The constraint of a type parameter declaration is kept as the declared type of its Declaration
        if (decl->declared_type.tag) {
            ret->param.state->constraint_node = &decl->declared_type;
        }
        *slot = ret;
    }
    return *slot;
}

Ty const* ty_synthetic_type_param(Checker* me, csview name) {
    return make_type_param(me, nullptr, checker_atom(me, name));
}

// References

Ty const* ty_reference(Checker* me, Ty const* target, TyList args) {
    check(target->kind == TY_DECLARED);
    if (!target->declared.type_params.len) return target;
    check(args.len == target->declared.type_params.len);
    u64 stack_key[32];
    u32 count = 2 + args.len;
    u64* key = count <= c_arraylen(stack_key) ? stack_key : malloc(sizeof(u64) * count);
    key[0] = KEY_REFERENCE;
    key[1] = target->id;
    for (u32 i = 0; i < args.len; i++) key[2 + i] = args.data[i]->id;
    void const** slot = intern_slot(me, &my types, key, count);
    if (key != stack_key) free(key);
    if (!*slot) {
        Ty* ret = ty_alloc(me, TY_REFERENCE, TYF_OBJECT | (ty_list_flags(args) & TYF_COULD_CONTAIN_TYPE_VARIABLES));
        ret->reference.target = target;
        ret->reference.args = ty_list_copy(me, args.data, args.len);
        ret->reference.state = arena_new(&my arena, TyObjectState);
        *slot = ret;
    }
    return *slot;
}

static Ty const* make_synthetic_target(Checker* me, char const* name) {
    Ty* ret = ty_alloc(me, TY_DECLARED, TYF_OBJECT);
    ret->declared.kind = TY_DECLARED_SYNTHETIC;
    ret->declared.name = checker_atom(me, csview_from(name));
    Ty const* param = ty_synthetic_type_param(me, c_sv("T"));
    ret->declared.type_params = ty_list_copy(me, &param, 1);
    ret->declared.state = arena_new(&my arena, TyObjectState);
    return ret;
}

static Ty const* global_generic_target(Checker* me, char const* name) {
    Declaration const* decl = scope_get_declaration(
        program_get_global_scope(my program),
        sv_make_span(csview_from(name))
    );
    if (decl && (decl->type_tag == DECLARATION_INTERFACE || decl->type_tag == DECLARATION_CLASS)) {
        Ty const* declared = ty_declared_type_of(me, decl);
        if (declared->kind == TY_DECLARED && declared->declared.type_params.len == 1) {
            return declared;
        }
    }
    return make_synthetic_target(me, name);
}

Ty const* ty_array(Checker* me, Ty const* element, bool readonly) {
    Ty const** target = readonly ? &my readonly_array_target : &my array_target;
    if (!*target) {
        *target = global_generic_target(me, readonly ? "ReadonlyArray" : "Array");
    }
    return ty_reference(me, *target, (TyList){ .data = &element, .len = 1 });
}

Ty const* ty_tuple_target(Checker* me, TupleShape const* shape) {
    u64 stack_key[64];
    u32 count = 3 + shape->arity * 2;
    u64* key = count <= c_arraylen(stack_key) ? stack_key : malloc(sizeof(u64) * count);
    key[0] = KEY_TUPLE_TARGET;
    key[1] = shape->arity;
    key[2] = shape->readonly;
    for (u32 i = 0; i < shape->arity; i++) {
        key[3 + i * 2] = shape->flags[i];
        key[4 + i * 2] = (u64)(uintptr_t)(shape->names ? shape->names[i] : nullptr);
    }
    void const** slot = intern_slot(me, &my types, key, count);
    if (key != stack_key) free(key);
    if (!*slot) {
        Ty* ret = ty_alloc(me, TY_DECLARED, TYF_OBJECT);
        ret->declared.kind = TY_DECLARED_TUPLE_TARGET;
        ret->declared.state = arena_new(&my arena, TyObjectState);
        TupleShape* copy = arena_new(&my arena, TupleShape);
        copy->arity = shape->arity;
        copy->readonly = shape->readonly;
        Ty const** params = arena_new_n(&my arena, Ty const*, shape->arity);
        unsigned char* flags = arena_new_n(&my arena, unsigned char, shape->arity);
        Atom const** names = shape->names ? arena_new_n(&my arena, Atom const*, shape->arity) : nullptr;
        for (u32 i = 0; i < shape->arity; i++) {
            params[i] = ty_synthetic_type_param(me, c_sv("T"));
            flags[i] = shape->flags[i];
            if (names) names[i] = shape->names[i];
        }
        copy->flags = flags;
        copy->names = names;
        ret->declared.tuple = copy;
        ret->declared.type_params = (TyList){ .data = params, .len = shape->arity };
        *slot = ret;
    }
    return *slot;
}

// Deferred types. The constructors resolve what they can and only make a deferred type for the rest

static Ty const* intern_deferred(Checker* me, u64 const* key, u32 count, TyKind kind, u32 operand_flags, Ty** fresh) {
    void const** slot = intern_slot(me, &my types, key, count);
    *fresh = nullptr;
    if (!*slot) {
        *fresh = ty_alloc(me, kind, TYF_DEFERRED | (operand_flags & TYF_COULD_CONTAIN_TYPE_VARIABLES));
        *slot = *fresh;
    }
    return *slot;
}

static Ty const* ty_prop_name_literal(Checker* me, Atom const* name) {
    return ty_string_literal(me, atom_sv(name));
}

Ty const* ty_keyof(Checker* me, Ty const* operand) {
    if (operand->flags & TYF_ANY) {
        Ty const* all[3] = { my intrinsics[TY_STRING], my intrinsics[TY_NUMBER], my intrinsics[TY_ES_SYMBOL] };
        return ty_union(me, all, 3, nullptr, (TyList){0});
    }
    if (operand->kind == TY_UNION) {
        // keyof (A | B) is keyof A & keyof B
        TyBuf parts = {0};
        for (u32 i = 0; i < operand->composite.types.len; i++) {
            ty_buf_push(&parts, ty_keyof(me, operand->composite.types.data[i]));
        }
        Ty const* ret = ty_intersection(me, parts.data, parts.len, nullptr, (TyList){0});
        ty_buf_drop(&parts);
        return ret;
    }
    if (operand->kind == TY_INTERSECTION) {
        TyBuf parts = {0};
        for (u32 i = 0; i < operand->composite.types.len; i++) {
            ty_buf_push(&parts, ty_keyof(me, operand->composite.types.data[i]));
        }
        Ty const* ret = ty_union(me, parts.data, parts.len, nullptr, (TyList){0});
        ty_buf_drop(&parts);
        return ret;
    }
    // The property names of `List<T>` don't depend on T: only type variables and generic mapped types defer
    bool named_object = (operand->kind == TY_REFERENCE && !ty_is_tuple(operand)) || operand->kind == TY_DECLARED;
    if ((operand->flags & TYF_OBJECT) && (!(operand->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) || named_object)) {
        Members const* members = ty_members(me, operand);
        if (!members->mapped) {
            TyBuf parts = {0};
            for (u32 i = 0; i < members->prop_count; i++) {
                ty_buf_push(&parts, ty_prop_name_literal(me, members->props[i]->name));
            }
            for (u32 i = 0; i < members->index_count; i++) {
                ty_buf_push(&parts, members->index[i].key);
                if (members->index[i].key->flags & TYF_STRING) {
                    // Anything indexable by string is indexable by number
                    ty_buf_push(&parts, my intrinsics[TY_NUMBER]);
                }
            }
            Ty const* ret = ty_union(me, parts.data, parts.len, nullptr, (TyList){0});
            ty_buf_drop(&parts);
            return ret;
        }
    }
    if (operand->flags & (TYF_NEVER)) {
        Ty const* all[3] = { my intrinsics[TY_STRING], my intrinsics[TY_NUMBER], my intrinsics[TY_ES_SYMBOL] };
        return ty_union(me, all, 3, nullptr, (TyList){0});
    }
    if (operand->flags & (TYF_UNKNOWN | TYF_NON_PRIMITIVE | TYF_VOID | TYF_NULLABLE)) {
        return my intrinsics[TY_NEVER];
    }
    // TODO: keyof a primitive is the keys of its apparent type (`String`, `Number`...), needs the globals
    u64 key[2] = { KEY_KEYOF, operand->id };
    Ty* fresh;
    Ty const* ret = intern_deferred(me, key, 2, TY_KEYOF, operand->flags, &fresh);
    if (fresh) fresh->keyof.operand = operand;
    return ret;
}

// Returns null if the new core can't tell yet
// Generic as far as deferring an indexed access goes: a type variable at the top level (tsc's isGenericObjectType /
// isGenericIndexType). `{ a: T }["a"]` isn't, it's `T`
static bool is_generic_operand(Checker* me, Ty const* ty, u32 depth) {
    if (ty->kind == TY_TYPE_PARAM || (ty->flags & TYF_DEFERRED)) return true;
    if (!(ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) || depth > 8) return false;
    if (ty->kind == TY_UNION || ty->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (is_generic_operand(me, ty->composite.types.data[i], depth + 1)) return true;
        }
        return false;
    }
    // A mapped type whose keys aren't known yet
    if (ty->kind == TY_ANONYMOUS) return ty_members(me, ty)->mapped != nullptr;
    return false;
}

// The access itself is ty_access() (access.c), the same lookup property access expressions get
static Ty const* try_resolve_indexed(Checker* me, Ty const* object, Ty const* index) {
    if ((object->flags & TYF_ANY) || (index->flags & TYF_ERROR)) return object->flags & TYF_ANY ? object : index;
    if (index->flags & TYF_NEVER) return index;
    if (is_generic_operand(me, object, 0) || is_generic_operand(me, index, 0)) return nullptr;
    return ty_access(me, object, index);
}

Ty const* ty_indexed(Checker* me, Ty const* object, Ty const* index) {
    Ty const* resolved = try_resolve_indexed(me, object, index);
    if (resolved) return resolved;
    u64 key[3] = { KEY_INDEXED, object->id, index->id };
    Ty* fresh;
    Ty const* ret = intern_deferred(me, key, 3, TY_INDEXED, object->flags | index->flags, &fresh);
    if (fresh) {
        fresh->indexed.object = object;
        fresh->indexed.index = index;
    }
    return ret;
}

// `x!`: the type without null and undefined. TODO: tsc intersects a type variable with `{}` under strictNullChecks
Ty const* ty_non_nullable(Checker* me, Ty const* ty) {
    if (ty->flags & TYF_NULLABLE) return ty_intrinsic(me, TY_NEVER);
    if (ty->flags & (TYF_TYPE_PARAM | TYF_DEFERRED)) {
        // HACK: tsc says `T & {}`. Only when the constraint allows null or undefined, and at the price of T
        Ty const* constraint = ty_base_constraint(me, ty);
        if (!constraint || constraint == ty) return ty;
        Ty const* stripped = ty_non_nullable(me, constraint);
        return stripped == constraint ? ty : stripped;
    }
    if (ty->kind != TY_UNION) return ty;
    TyList members = ty->composite.types;
    Ty const** kept = malloc(sizeof(Ty const*) * members.len);
    u32 len = 0;
    for (u32 i = 0; i < members.len; i++) {
        if (!(members.data[i]->flags & TYF_NULLABLE)) kept[len++] = members.data[i];
    }
    Ty const* ret = len == members.len ? ty : ty_union(me, kept, len, nullptr, (TyList){0});
    free(kept);
    return ret;
}

Ty const* ty_cond_branch_type(Checker* me, TyCondBranch* branch) {
    if (!branch->node || branch->state == LAZY_STATE_DONE) return branch->type;
    if (branch->state == LAZY_STATE_IN_FLIGHT) return ty_intrinsic(me, TY_ERROR);
    branch->state = LAZY_STATE_IN_FLIGHT;
    branch->type = ty_instantiate(me, ty_from_node(me, branch->node, branch->ctx), branch->mapper);
    branch->state = LAZY_STATE_DONE;
    return branch->type;
}

static u32 cond_branch_key(u64* key, TyCondBranch const* branch) {
    if (!branch->node) {
        key[0] = branch->type->id;
        key[1] = 0;
        return 2;
    }
    u32 count = 0;
    key[count++] = (u64)(uintptr_t)branch->node;
    key[count++] = branch->mapper ? branch->mapper->len : 0;
    for (u32 i = 0; branch->mapper && i < branch->mapper->len && count + 2 <= 24; i++) {
        key[count++] = branch->mapper->sources[i]->id;
        key[count++] = branch->mapper->targets[i]->id;
    }
    return count;
}

// Resolves right away unless the check type is generic (conditional.c)
Ty const* ty_conditional(
    Checker* me, Ty const* check_type, Ty const* extends, TyCondBranch const* if_true, TyCondBranch const* if_false,
    Ty const* root, TyMapper const* mapper, bool distributive
) {
    Ty const* resolved = ty_conditional_resolve(me, check_type, extends, if_true, if_false);
    if (resolved) return resolved;
    u64 key[5 + 24 + 24] = { KEY_CONDITIONAL, check_type->id, extends->id, distributive, root ? root->id : 0 };
    u32 count = 5;
    count += cond_branch_key(key + count, if_true);
    count += cond_branch_key(key + count, if_false);
    u32 flags = check_type->flags | extends->flags | TYF_COULD_CONTAIN_TYPE_VARIABLES;
    Ty* fresh;
    Ty const* ret = intern_deferred(me, key, count, TY_CONDITIONAL, flags, &fresh);
    if (fresh) {
        fresh->conditional.check = check_type;
        fresh->conditional.extends = extends;
        TyCondBranch* branches = arena_new_n(&my arena, TyCondBranch, 2);
        branches[0] = *if_false;
        branches[1] = *if_true;
        fresh->conditional.branches = branches;
        fresh->conditional.distributive = distributive;
        fresh->conditional.root = root ? root : fresh;
        fresh->conditional.mapper = root ? mapper : nullptr;
    }
    return ret;
}
