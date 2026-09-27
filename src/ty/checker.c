#include "ty/ty_internal.h"
#include <math.h>
#include <sys/mman.h>
#include "lazy_type.h"
#include "program.h"
#include "scope.h"

// Arena

#define ARENA_CHUNK_SIZE (1024 * 1024)

struct ArenaChunk {
    ArenaChunk* next;
    size_t used;
    size_t cap;
    size_t mapped; // Bytes mapped for the chunk, header included
    char data[];
};

void* arena_alloc(Arena* me, size_t size) {
    size = (size + 7) & ~(size_t)7;
    my total += size;
    if (!my head || my head->used + size > my head->cap) {
        size_t cap = size > ARENA_CHUNK_SIZE ? size : ARENA_CHUNK_SIZE;
        // Straight from mmap: the kernel's pages are zero already (calloc() would memset them), and the ones the
        // arena never reaches are never touched
        size_t mapped = sizeof(ArenaChunk) + cap;
        void* memory = mmap(nullptr, mapped, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        panic_if(memory == MAP_FAILED, "out of memory");
        ArenaChunk* chunk = memory;
        chunk->cap = cap;
        chunk->mapped = mapped;
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
        munmap(i, i->mapped);
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

static void swap_twin_ids(Ty const* fresh, Ty const* regular) {
    u32 id = fresh->id;
    ((Ty*)fresh)->id = regular->id;
    ((Ty*)regular)->id = id;
}

Checker* checker_new(Program* program) {
    Checker* me = calloc(1, sizeof(Checker));
    panic_if(!me, "out of memory");
    my program = program;
    my binder = program_get_binder(program);
    my next_type_id = 1;
    my intrinsics[TY_ANY] = make_intrinsic(me, "any", TYF_ANY);
    my intrinsics[TY_AUTO] = make_intrinsic(me, "any", TYF_ANY);
    my intrinsics[TY_ERROR] = make_intrinsic(me, "any", TYF_ANY | TYF_ERROR);
    my intrinsics[TY_CIRCULAR] = make_intrinsic(me, "any", TYF_ANY);
    my intrinsics[TY_UNKNOWN_] = make_intrinsic(me, "unknown", TYF_UNKNOWN);
    my intrinsics[TY_UNDEFINED] = make_intrinsic(me, "undefined", TYF_UNDEFINED);
    my intrinsics[TY_NULL] = make_intrinsic(me, "null", TYF_NULL);
    my intrinsics[TY_STRING] = make_intrinsic(me, "string", TYF_STRING);
    my intrinsics[TY_NUMBER] = make_intrinsic(me, "number", TYF_NUMBER);
    my intrinsics[TY_BIGINT] = make_intrinsic(me, "bigint", TYF_BIGINT);
    Value false_value = { .tag = VALUE_BOOLEAN, .boolean = false };
    Value true_value = { .tag = VALUE_BOOLEAN, .boolean = true };
    // tsc makes each fresh boolean before its regular twin (only those are intrinsics): the ids are swapped to match,
    // which is fine before anything else refers to them
    my intrinsics[TY_FALSE] = ty_literal(me, &false_value);
    swap_twin_ids(ty_fresh_literal(me, my intrinsics[TY_FALSE]), my intrinsics[TY_FALSE]);
    my intrinsics[TY_TRUE] = ty_literal(me, &true_value);
    swap_twin_ids(ty_fresh_literal(me, my intrinsics[TY_TRUE]), my intrinsics[TY_TRUE]);
    my intrinsics[TY_BOOLEAN] = ty_union(me, &my intrinsics[TY_FALSE], 2, nullptr, (TyList){0});
    my intrinsics[TY_ES_SYMBOL] = make_intrinsic(me, "symbol", TYF_ES_SYMBOL);
    my intrinsics[TY_VOID] = make_intrinsic(me, "void", TYF_VOID);
    my intrinsics[TY_NEVER] = make_intrinsic(me, "never", TYF_NEVER);
    my intrinsics[TY_SILENT_NEVER] = make_intrinsic(me, "never", TYF_NEVER | TYF_NON_INFERRABLE);
    my intrinsics[TY_IMPLICIT_NEVER] = make_intrinsic(me, "never", TYF_NEVER);
    my intrinsics[TY_UNREACHABLE_NEVER] = make_intrinsic(me, "never", TYF_NEVER);
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
    free(my intersection_props.entries);
    for (u32 i = 0; i < TY_RELATION_COUNT; i++) relation_cache_drop(&my relations[i]);
    free(my flow_loops);
    free(my aliases_in_flight);
    free(my flow_shared);
    arena_drop(&my arena);
    free(me);
}

static Ty const* literal_type(Checker* me, Value const* value, Symbol const* enum_decl, Symbol const* enum_member) {
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

Ty const* ty_enum_literal(Checker* me, Value const* value, Symbol const* enum_decl, Symbol const* member) {
    return literal_type(me, value, enum_decl, member);
}

// The type of an enum member whose value isn't a compile time constant (tsc's computed enum type, TypeFlags.Enum).
// Also the type of an enum without members, with the enum as its own member
Ty const* ty_enum_computed_member(Checker* me, Symbol const* enum_decl, Symbol const* member) {
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

Ty const* ty_unique_symbol(Checker* me, UniqueSymbolDecl const* decl) {
    if (!decl->key) return my intrinsics[TY_ES_SYMBOL];
    u64 key[2] = { KEY_UNIQUE_SYMBOL, (u64)(uintptr_t)decl->key };
    void const** slot = intern_slot(me, &my types, key, 2);
    if (!*slot) {
        Ty* ret = ty_alloc(me, TY_LITERAL, TYF_UNIQUE_ES_SYMBOL);
        ret->literal.value = (Value){ .tag = VALUE_UNDEFINED };
        ret->literal.unique = decl;
        csview name = span_valid(decl->member) ? decl->member.sv : decl->owner ? decl->owner->name : c_sv("");
        cstr escaped = cstr_d("__@", name, "@", ret->id);
        ret->literal.string = checker_atom(me, cstr_sv(&escaped));
        cstr_drop(&escaped);
        *slot = ret;
        u64 name_key[2] = { KEY_UNIQUE_SYMBOL_NAME, (u64)(uintptr_t)ret->literal.string };
        *intern_slot(me, &my links, name_key, 2) = ret;
    }
    return *slot;
}

Ty const* ty_widened_unique_symbol(Checker* me, Ty const* ty) {
    if (ty->flags & TYF_UNIQUE_ES_SYMBOL) return my intrinsics[TY_ES_SYMBOL];
    if (ty->kind == TY_UNION) {
        bool any = false;
        for (u32 i = 0; i < ty->composite.types.len; i++) any |= (ty->composite.types.data[i]->flags & TYF_UNIQUE_ES_SYMBOL) != 0;
        if (!any) return ty;
        TyBuf buf = {0};
        for (u32 i = 0; i < ty->composite.types.len; i++) ty_buf_push(&buf, ty_widened_unique_symbol(me, ty->composite.types.data[i]));
        Ty const* ret = ty_union(me, buf.data, buf.len, nullptr, (TyList){0});
        ty_buf_drop(&buf);
        return ret;
    }
    return ty;
}

Ty const* ty_fresh_literal(Checker* me, Ty const* ty) {
    if (ty->kind != TY_LITERAL || (ty->flags & (TYF_FRESH_LITERAL | TYF_UNIQUE_ES_SYMBOL))) return ty;
    u64 key[2] = { KEY_FRESH_LITERAL, (u64)(uintptr_t)ty };
    void const** slot = intern_slot(me, &my types, key, 2);
    if (!*slot) {
        Ty* ret = ty_alloc(me, TY_LITERAL, ty->flags | TYF_FRESH_LITERAL);
        ret->literal = ty->literal;
        ret->literal.regular = ty;
        *slot = ret;
    }
    return *slot;
}

Ty const* ty_regular(Checker* me, Ty const* ty) {
    if (!(ty->flags & TYF_FRESH_LITERAL)) return ty;
    if (ty->kind == TY_LITERAL) return ty->literal.regular;
    check(ty->kind == TY_UNION);
    u64 key[2] = { KEY_REGULAR_UNION, (u64)(uintptr_t)ty };
    void const* known = intern_get(&my types, key, 2);
    if (known) return known;
    TyList members = ty->composite.types;
    TyBuf buf = {0};
    for (u32 i = 0; i < members.len; i++) ty_buf_push(&buf, ty_regular(me, members.data[i]));
    Ty const* ret = ty_union(me, buf.data, buf.len, ty->alias, ty->alias_args);
    ty_buf_drop(&buf);
    *intern_slot(me, &my types, key, 2) = ret;
    return ret;
}

static bool sorted_contains(TyBuf const* buf, u32 len, Ty const* t) {
    u32 lo = 0, hi = len;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        if (buf->data[mid]->id == t->id) return buf->data[mid] == t;
        if (buf->data[mid]->id < t->id) lo = mid + 1; else hi = mid;
    }
    return false;
}

Ty const* ty_string_literal(Checker* me, csview sv) {
    Value value = { .tag = VALUE_STRING, .string = cstr_from_sv(sv) };
    Ty const* ret = ty_literal(me, &value);
    value_drop(&value);
    return ret;
}

// Unions and intersections

// Sorts by id. The input is usually a few sorted runs back to back (the members of the unions being combined, plus a
// few more), so this is a natural merge sort: it merges the runs that are there instead of sorting from scratch
static void sort_by_id(Ty const** data, u32 len) {
    if (len <= 8) {
        for (u32 i = 1; i < len; i++) {
            Ty const* t = data[i];
            u32 j = i;
            for (; j && data[j - 1]->id > t->id; j--) data[j] = data[j - 1];
            data[j] = t;
        }
        return;
    }
    // Run boundaries: run k is [bounds[k], bounds[k + 1])
    u32* bounds = malloc(sizeof(u32) * (len + 1));
    Ty const** tmp = malloc(sizeof(Ty const*) * len);
    panic_if(!bounds || !tmp, "out of memory");
    u32 runs = 0;
    bounds[runs++] = 0;
    for (u32 i = 1; i < len; i++) if (data[i - 1]->id > data[i]->id) bounds[runs++] = i;
    bounds[runs] = len;
    Ty const** src = data;
    Ty const** dst = tmp;
    while (runs > 1) {
        u32 merged = 0;
        for (u32 k = 0; k < runs; k += 2) {
            u32 lo = bounds[k], mid = bounds[k + 1], hi = k + 2 <= runs ? bounds[k + 2] : mid;
            u32 a = lo, b = mid, o = lo;
            while (a < mid && b < hi) dst[o++] = src[b]->id < src[a]->id ? src[b++] : src[a++];
            while (a < mid) dst[o++] = src[a++];
            while (b < hi) dst[o++] = src[b++];
            bounds[merged++] = lo;
        }
        bounds[merged] = len;
        runs = merged;
        Ty const** swap = src;
        src = dst;
        dst = swap;
    }
    if (src != data) memcpy(data, src, sizeof(Ty const*) * len);
    free(tmp);
    free(bounds);
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
    Checker* me, TyKind kind, Ty const* const* types, u32 len, Symbol const* alias, TyList alias_args
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
        u32 member_flags = flags;
        flags = (flags | ty_list_flags(alias_args)) & TYF_COULD_CONTAIN_TYPE_VARIABLES;
        flags |= kind == TY_UNION ? TYF_UNION | (member_flags & (TYF_FRESH_LITERAL | TYF_FRESH_OBJECT)) : TYF_INTERSECTION;
        flags |= member_flags & TYF_NON_INFERRABLE;
        // Any two boolean literals, fresh or not, are `boolean` (tsc's getUnionTypeFromSortedList)
        if (kind == TY_UNION && len == 2 && (types[0]->flags & types[1]->flags & TYF_BOOLEAN_LITERAL)) {
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

// String literals that a pattern in the same union already has (tsc's removeStringLiteralsMatchedByTemplateLiterals):
// `"ab" | `a${string}`` is `a${string}`. Returns the new length
static u32 remove_literals_matched_by_patterns(Checker* me, Ty const** types, u32 len) {
    bool any_pattern = false;
    for (u32 i = 0; i < len && !any_pattern; i++) any_pattern = ty_is_pattern_literal(types[i]);
    if (!any_pattern) return len;
    u32 out = 0;
    for (u32 i = 0; i < len; i++) {
        Ty const* t = types[i];
        bool matched = false;
        if (t->kind == TY_LITERAL && (t->flags & TYF_STRING_LITERAL)) {
            for (u32 j = 0; j < len && !matched; j++) {
                Ty const* pattern = types[j];
                if (!ty_is_pattern_literal(pattern)) continue;
                matched = pattern->kind == TY_TEMPLATE_LITERAL ?
                    ty_template_matches(me, t, pattern) : ty_string_mapping_contains(me, t, pattern);
            }
        }
        if (!matched) types[out++] = t;
    }
    return out;
}

// Only the cheap reductions: flatten, dedupe, absorb any/unknown/never, drop literals next to their primitive.
// Subtype reduction is a separate operation (TODO, the legacy type_composite() does its own through the relation).
Ty const* ty_union(Checker* me, Ty const* const* types, u32 len, Symbol const* alias, TyList alias_args) {
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
        // The members of a union are sorted: a filtered union (narrowing) comes in sorted already
        bool sorted = true;
        for (u32 i = 1; i < buf.len && sorted; i++) sorted = buf.data[i - 1]->id <= buf.data[i]->id;
        if (!sorted) sort_by_id(buf.data, buf.len);
        bool strict_null_checks = compiler_options()->strict_null_checks;
        if (includes & TYF_FRESH_LITERAL) {
            // Drop fresh literals whose regular twin is here too, before the rest compacts the (sorted) buffer
            // (tsc's removeRedundantLiteralTypes)
            bool* drop = calloc(buf.len, sizeof(bool));
            panic_if(!drop, "out of memory");
            for (u32 i = 0; i < buf.len; i++) {
                drop[i] = ty_is_fresh(buf.data[i]) && sorted_contains(&buf, buf.len, buf.data[i]->literal.regular);
            }
            u32 kept = 0;
            for (u32 i = 0; i < buf.len; i++) if (!drop[i]) buf.data[kept++] = buf.data[i];
            buf.len = kept;
            free(drop);
        }
        u32 out = 0;
        for (u32 i = 0; i < buf.len; i++) {
            Ty const* t = buf.data[i];
            if (out && buf.data[out - 1] == t) continue;
            if (t->kind == TY_INTRINSIC && (t->flags & TYF_NEVER)) continue;
            // Without strictNullChecks null and undefined are in every type, so a union doesn't keep them
            if (!strict_null_checks && (t->flags & TYF_NULLABLE)) continue;
            if ((t->flags & (TYF_TEMPLATE_LITERAL | TYF_STRING_MAPPING)) && (includes & TYF_STRING)) continue;
            if (t->kind == TY_LITERAL && (
                ((t->flags & TYF_STRING_LITERAL) && (includes & TYF_STRING)) ||
                ((t->flags & TYF_NUMBER_LITERAL) && (includes & TYF_NUMBER)) ||
                ((t->flags & TYF_BIGINT_LITERAL) && (includes & TYF_BIGINT)) ||
                ((t->flags & TYF_UNIQUE_ES_SYMBOL) && (includes & TYF_ES_SYMBOL))
            )) continue;
            buf.data[out++] = t;
        }
        if ((includes & TYF_STRING_LITERAL) && (includes & (TYF_TEMPLATE_LITERAL | TYF_STRING_MAPPING))) {
            out = remove_literals_matched_by_patterns(me, buf.data, out);
        }
        if (out == 0) {
            // Nothing but null and undefined (without strictNullChecks): null wins, like in tsc's getUnionType
            TyIntrinsic only = TY_NEVER;
            if (!strict_null_checks) only = includes & TYF_NULL ? TY_NULL : includes & TYF_UNDEFINED ? TY_UNDEFINED : TY_NEVER;
            ret = my intrinsics[only];
        } else if (out == 1) {
            ret = buf.data[0];
        } else {
            ret = intern_composite(me, TY_UNION, buf.data, out, alias, alias_args);
        }
    }
    ty_buf_drop(&buf);
    return ret;
}

// Some members of `of`, in its order (narrowing's filters): they are sorted and reduced already, so there's only the
// interning left to do. Rebuilding a filtered 300-member enum union through ty_union() is most of what narrowing an
// enum by `===` costs
Ty const* ty_union_filtered(Checker* me, Ty const* of, Ty const* const* kept, u32 len) {
    check(of->kind == TY_UNION);
    if (len == of->composite.types.len) return of;
    if (len == 0) return my intrinsics[TY_NEVER];
    if (len == 1) return kept[0];
    return intern_composite(me, TY_UNION, kept, len, nullptr, (TyList){0});
}

// Which disjoint family of primitives a type belongs to, 0 if none
static u32 primitive_family(Ty const* t) {
    if (t->kind != TY_INTRINSIC && t->kind != TY_LITERAL) return 0;
    if (t->flags & (TYF_STRING | TYF_STRING_LITERAL)) return TYF_STRING;
    if (t->flags & (TYF_NUMBER | TYF_NUMBER_LITERAL)) return TYF_NUMBER;
    if (t->flags & (TYF_BIGINT | TYF_BIGINT_LITERAL)) return TYF_BIGINT;
    if (t->flags & TYF_BOOLEAN_LITERAL) return TYF_BOOLEAN_LITERAL;
    if (t->flags & TYF_UNIQUE_ES_SYMBOL) return TYF_ES_SYMBOL;
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

// tsc's isLiteralType: what a property can discriminate by
static bool is_literal_like(Ty const* t) {
    if (t->flags & (TYF_BOOLEAN | TYF_ENUM_LITERAL)) return true;
    if (t->kind == TY_UNION) {
        for (u32 i = 0; i < t->composite.types.len; i++) {
            if (!is_literal_like(t->composite.types.data[i])) return false;
        }
        return true;
    }
    return t->kind == TY_LITERAL || (t->kind == TY_INTRINSIC && (t->flags & TYF_NULLABLE));
}

static bool prop_judgeable(Checker* me, Prop const* prop) {
    while (prop->type_state == LAZY_STATE_NOT_STARTED && prop->target) prop = prop->target;
    if (prop->type_state == LAZY_STATE_IN_FLIGHT) return false;
    if (prop->type_state == LAZY_STATE_DONE || !prop->node || prop->lazy || prop->function || prop->overload_count || prop->symbol) {
        return true;
    }
    return !ty_node_reaches_alias_in_flight(me, prop->node);
}

// Whether resolving the members of a type literal would lower an index signature (they're lowered eagerly, see hacks.md)
// or a mapped type's template that reaches an alias being resolved, or the arguments of a deferred reference that do
static bool members_judgeable(Checker* me, Ty const* ty) {
    // A deferred reference's arguments may need the alias being resolved (`type T = T[] & { x: string }`)
    if (ty->kind == TY_REFERENCE && ty->reference.deferred && ty->reference.deferred->state != LAZY_STATE_DONE) {
        return !ty_node_reaches_alias_in_flight(me, ty->reference.deferred->node);
    }
    Ty const* root = ty;
    while (root->kind == TY_ANONYMOUS && root->anonymous.target && !root->anonymous.applies_type_args) root = root->anonymous.target;
    if (root->kind != TY_ANONYMOUS || !root->anonymous.node || root->anonymous.node->tag != TYPE_INTERFACE) return true;
    for_each (i, vec_interface_member, &root->anonymous.node->interface.members) {
        // (A mapped type over non-literal keys is an index signature too)
        bool index = i->key.tag == INTERFACE_KEY_INDEX || i->key.tag == INTERFACE_KEY_MAPPED;
        if (index && ty_node_reaches_alias_in_flight(me, &i->type)) return false;
    }
    return true;
}

static Prop const* root_declaration(Prop const* prop) {
    while (prop->target) prop = prop->target;
    return prop;
}

// A property of types[i] that a later member declares separately, one of them privately (tsc's
// isConflictingPrivateProperty: a synthetic property with no single value declaration that contains a private one)
static bool has_conflicting_private(Checker* me, Prop const* prop, Ty const* const* types, u32 i, u32 len) {
    for (u32 j = i + 1; j < len; j++) {
        Ty const* b = types[j];
        if (!(b->flags & TYF_OBJECT) || (b->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) || !members_judgeable(me, b)) continue;
        Prop const* other = ty_get_prop(me, b, prop->name);
        if (other && ((prop->flags | other->flags) & PROP_PRIVATE) && root_declaration(prop) != root_declaration(other)) {
            return true;
        }
    }
    return false;
}

// Object types that both have a required property whose types are disjoint, one of them a literal (`{ kind: "a" }
// & { kind: "b" }`): the intersection is never (tsc's isDiscriminantWithNeverType, via getReducedType). Remembered
// per member list, since it looks at every property
static bool has_conflicting_discriminants(Checker* me, Ty const* const* types, u32 len) {
    u32 objects = 0;
    for (u32 i = 0; i < len; i++) {
        if ((types[i]->flags & TYF_OBJECT) && !(types[i]->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) objects++;
    }
    if (objects < 2 || len > 30) return false;
    u64 key[32] = { KEY_DISCRIMINANT_CONFLICT };
    for (u32 i = 0; i < len; i++) key[1 + i] = types[i]->id;
    void const** slot = intern_slot(me, &my links, key, 1 + len);
    if (*slot) return (uintptr_t)*slot == 2;
    *slot = (void const*)1; // (While working it out: no conflict)
    bool conflict = false;
    // HACK: tsc reduces lazily (getReducedType), this runs when the intersection is made, which can be while one of
    // the properties or an alias it names is being lowered (`type B = X & { e: A }; type A = I | B`): that property
    // can't be judged yet, and its type mustn't be asked for (it would be the error type for good)
    bool skipped = false;
    for (u32 i = 0; i < len && !conflict; i++) {
        Ty const* a = types[i];
        if (!(a->flags & TYF_OBJECT) || (a->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) continue;
        if (!members_judgeable(me, a)) {
            skipped = true;
            continue;
        }
        Members const* members = ty_members(me, a);
        for (u32 p = 0; p < members->prop_count && !conflict; p++) {
            Prop const* prop = members->props[p];
            conflict = has_conflicting_private(me, prop, types, i, len);
            if (prop->flags & PROP_OPTIONAL) continue;
            if (!prop_judgeable(me, prop)) {
                skipped = true;
                continue;
            }
            Ty const* a_type = prop_type(me, prop);
            for (u32 j = i + 1; j < len && !conflict; j++) {
                Ty const* b = types[j];
                if (!(b->flags & TYF_OBJECT) || (b->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) continue;
                if (!members_judgeable(me, b)) {
                    skipped = true;
                    continue;
                }
                Prop const* other = ty_get_prop(me, b, prop->name);
                if (!other || (other->flags & PROP_OPTIONAL)) continue;
                if (!prop_judgeable(me, other)) {
                    skipped = true;
                    continue;
                }
                Ty const* b_type = prop_type(me, other);
                if (!is_literal_like(a_type) && !is_literal_like(b_type)) continue;
                Ty const* both[2] = { a_type, b_type };
                conflict = (ty_intersection(me, both, 2, nullptr, (TyList){0})->flags & TYF_NEVER) != 0;
            }
        }
    }
    // (The slot may have moved while the members were resolved)
    if (skipped && !conflict) {
        *intern_slot(me, &my links, key, 1 + len) = nullptr; // Asked again once the property is done
        return false;
    }
    *intern_slot(me, &my links, key, 1 + len) = (void const*)(uintptr_t)(conflict ? 2 : 1);
    return conflict;
}

// The templates and mappings of an intersection with the string literal `literal` go if the literal is one of theirs,
// true if a pattern doesn't have it (the intersection is empty)
static bool extract_redundant_templates(Checker* me, Ty const** types, u32* len, Ty const* literal) {
    u32 out = 0;
    for (u32 i = 0; i < *len; i++) {
        Ty const* t = types[i];
        if (t->flags & (TYF_TEMPLATE_LITERAL | TYF_STRING_MAPPING)) {
            if (ty_is_related(me, literal, t, TY_RELATION_SUBTYPE)) continue;
            if (ty_is_pattern_literal(t)) return true;
        }
        types[out++] = t;
    }
    *len = out;
    return false;
}

// tsc's isEmptyAnonymousObjectType: `{}` (a type literal or anonymous object without members)
bool ty_is_empty_anonymous_object(Checker* me, Ty const* ty) {
    if (ty->kind != TY_ANONYMOUS) return false;
    // A type literal goes by its syntax (tsc's isEmptyTypeLiteralSymbol): its members may not be resolvable yet, `type
    // Foo = { x: string } & { [x: number]: Foo }` (the same for its instantiations)
    Ty const* root = ty;
    while (root->anonymous.target && !root->anonymous.applies_type_args) root = root->anonymous.target;
    if (root->kind == TY_ANONYMOUS && root->anonymous.node && root->anonymous.node->tag == TYPE_INTERFACE) {
        return !vec_interface_member_size(&root->anonymous.node->interface.members);
    }
    Members const* members = ty_members(me, ty);
    return !members->prop_count && !members->call_count && !members->construct_count && !members->index_count &&
        !members->mapped;
}

static Ty const* intersection(Checker* me, Ty const* const* types, u32 len, Symbol const* alias, TyList alias_args, bool keep_empty_object);

Ty const* ty_intersection(Checker* me, Ty const* const* types, u32 len, Symbol const* alias, TyList alias_args) {
    return intersection(me, types, len, alias, alias_args, false);
}

Ty const* ty_intersection_keeping_empty_object(Checker* me, Ty const* const* types, u32 len, Symbol const* alias, TyList alias_args) {
    return intersection(me, types, len, alias, alias_args, true);
}

static bool union_has(Ty const* ty, u32 flag) {
    if (ty->kind != TY_UNION) return false;
    for (u32 i = 0; i < ty->composite.types.len; i++) {
        if (ty->composite.types.data[i]->flags & flag) return true;
    }
    return false;
}

// tsc's getIntersectionType with unions among `buf` (not yet distributed), before the cross product: when every member
// is a union with undefined (or null) in it, that is taken out, `(A | undefined) & (B | undefined)` being
// `(A & B) | undefined`; three or more members are intersected in halves, which usually makes far fewer combinations
// than the whole cross product; one of 100000 or more combinations is the error type (tsc's TS2590, not reported).
// Null if the plain cross product it is. `len`: how many types the intersection was asked for
static Ty const* intersect_unions(Checker* me, TyBuf* buf, u32 len, Symbol const* alias, TyList alias_args) {
    u32 flags[2] = { TYF_UNDEFINED, TYF_NULL };
    for (u32 f = 0; f < 2; f++) {
        bool all = true;
        for (u32 i = 0; i < buf->len && all; i++) all = union_has(buf->data[i], flags[f]);
        if (!all) continue;
        Ty const** rest = malloc(sizeof(Ty const*) * buf->len);
        panic_if(!rest, "out of memory");
        for (u32 i = 0; i < buf->len; i++) {
            TyList members = buf->data[i]->composite.types;
            Ty const** kept = malloc(sizeof(Ty const*) * members.len);
            panic_if(!kept, "out of memory");
            u32 kept_len = 0;
            for (u32 j = 0; j < members.len; j++) {
                if (!(members.data[j]->flags & flags[f])) kept[kept_len++] = members.data[j];
            }
            rest[i] = ty_union(me, kept, kept_len, nullptr, (TyList){0});
            free(kept);
        }
        Ty const* both[2] = { ty_intersection(me, rest, buf->len, nullptr, (TyList){0}),
            my intrinsics[f == 0 ? TY_UNDEFINED : TY_NULL] };
        free(rest);
        return ty_union(me, both, 2, alias, alias_args);
    }
    if (buf->len >= 3 && len > 2) {
        u32 middle = buf->len / 2;
        Ty const* halves[2] = {
            ty_intersection(me, buf->data, middle, nullptr, (TyList){0}),
            ty_intersection(me, buf->data + middle, buf->len - middle, nullptr, (TyList){0}),
        };
        return intersection(me, halves, 2, alias, alias_args, false);
    }
    // (tsc's checkCrossProductUnion)
    double size = 1;
    for (u32 i = 0; i < buf->len; i++) {
        if (buf->data[i]->kind == TY_UNION) size *= buf->data[i]->composite.types.len;
    }
    return size >= 100000 ? my intrinsics[TY_ERROR] : nullptr;
}

static Ty const* intersection(Checker* me, Ty const* const* types, u32 len, Symbol const* alias, TyList alias_args, bool keep_empty_object) {
    if (len == 1) return types[0];
    TyBuf buf = {0};
    ty_buf_add_flattened(&buf, TY_INTERSECTION, types, len);
    // The members of an intersection are regular (freshLiteralTypesInIntersections)
    for (u32 i = 0; i < buf.len; i++) buf.data[i] = ty_regular(me, buf.data[i]);
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
        // (The silent never, what inference hasn't found yet, survives: tsc's getIntersectionType)
        bool silent = false;
        for (u32 i = 0; i < buf.len; i++) silent |= buf.data[i] == my intrinsics[TY_SILENT_NEVER];
        ret = my intrinsics[silent ? TY_SILENT_NEVER : TY_NEVER];
    } else if (includes & TYF_ANY) {
        ret = my intrinsics[includes_error ? TY_ERROR : TY_ANY];
    } else if (first_union && (ret = intersect_unions(me, &buf, len, alias, alias_args))) {
        // (Handled)
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
        if (!is_never && (includes & (TYF_TEMPLATE_LITERAL | TYF_STRING_MAPPING))) {
            // A template or mapping and a primitive of another family is never, and one next to `string` is itself
            u32 kept = 0;
            for (u32 i = 0; i < out && !is_never; i++) {
                u32 f = primitive_family(buf.data[i]);
                if (f && f != TYF_STRING) is_never = true;
                if (buf.data[i]->kind == TY_INTRINSIC && f == TYF_STRING) continue;
                buf.data[kept++] = buf.data[i];
            }
            out = kept;
            // `get${string}` & "getX" is "getX", and with "setX" never (tsc's extractRedundantTemplateLiterals)
            if (literal && !is_never) is_never = extract_redundant_templates(me, buf.data, &out, literal);
        }
        // Nothing is both an object and null or undefined
        bool strict = compiler_options()->strict_null_checks;
        if (strict && (includes & TYF_NULLABLE) && (includes & (TYF_OBJECT | TYF_NON_PRIMITIVE))) {
            is_never = true;
        }
        // `{}` next to something that is never null or undefined adds nothing (tsc's removeRedundantSupertypes):
        // NonNullable<1> is 1
        u32 empty_objects = 0, non_nullable = 0;
        for (u32 i = 0; i < out; i++) {
            if (ty_is_empty_anonymous_object(me, buf.data[i])) {
                empty_objects++;
            } else if (buf.data[i]->flags & (TYF_STRING_LIKE | TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_BIGINT |
                TYF_BIGINT_LITERAL | TYF_BOOLEAN | TYF_BOOLEAN_LITERAL | TYF_ENUM | TYF_ES_SYMBOL_LIKE | TYF_OBJECT |
                TYF_NON_PRIMITIVE)
            ) {
                non_nullable++;
            }
        }
        if (!is_never && empty_objects && non_nullable && !keep_empty_object) {
            u32 kept = 0;
            for (u32 i = 0; i < out; i++) {
                if (!ty_is_empty_anonymous_object(me, buf.data[i])) buf.data[kept++] = buf.data[i];
            }
            out = kept;
        }
        // Without strictNullChecks null and undefined are in every type: `T[] & null` is null (`{} & null` never)
        if (!strict && !is_never && (includes & TYF_NULLABLE)) {
            ret = my intrinsics[empty_objects ? TY_NEVER : includes & TYF_UNDEFINED ? TY_UNDEFINED : TY_NULL];
            ty_buf_drop(&buf);
            return ret;
        }
        if (!is_never && out >= 2 && has_conflicting_discriminants(me, buf.data, out)) is_never = true;
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

static Ty const* make_type_param(Checker* me, Symbol const* decl, Atom const* name) {
    Ty* ret = ty_alloc(me, TY_TYPE_PARAM, TYF_TYPE_PARAM | TYF_COULD_CONTAIN_TYPE_VARIABLES);
    ret->param.symbol = decl;
    ret->param.name = name;
    ret->param.state = arena_new(&my arena, TyParamState);
    return ret;
}

Ty const* ty_type_param(Checker* me, Symbol const* decl) {
    u64 key[2] = { KEY_TYPE_PARAM, (u64)(uintptr_t)decl };
    void const** slot = intern_slot(me, &my links, key, 2);
    if (!*slot) {
        Ty const* ret = make_type_param(me, decl, checker_atom(me, decl->name_span.sv));
        // `infer U extends X`. Declared type parameters read their GenericItem (TyParamState.item), mapped type keys
        // get their constraint from whoever lowers the mapped type
        InferredType const* inferred = symbol_find_node(decl, SYMBOL_DECL_INFER);
        if (inferred && inferred->extends.tag != TYPE_ANY) ret->param.state->constraint_node = &inferred->extends;
        // The key of a mapped type is constrained by what it iterates over (as declared: instantiations share it)
        InterfaceMember const* mapped = symbol_find_node(decl, SYMBOL_DECL_MAPPED_KEY);
        if (mapped) ret->param.state->constraint_node = &mapped->key.mapped.in_type;
        ret->param.state->is_infer = inferred != nullptr;
        ret->param.state->item = symbol_find_node(decl, SYMBOL_DECL_TYPE_PARAMETER);
        *slot = ret;
    }
    return *slot;
}

Ty const* ty_synthetic_type_param(Checker* me, csview name) {
    return make_type_param(me, nullptr, checker_atom(me, name));
}

Ty* ty_clone_type_param(Checker* me, Ty const* param, TyMapper const* mapper, Atom const* name) {
    Ty* ret = (Ty*)make_type_param(me, param->param.symbol, name ? name : param->param.name);
    ret->param.state->clone_of = param;
    ret->param.state->clone_mapper = mapper;
    ret->param.state->is_infer = param->param.state->is_infer;
    return ret;
}

bool ty_param_is_bounded(Ty const* param) {
    TyParamState const* state = param->param.state;
    if (state->clone_of || state->is_this) return true;
    if (state->item) return state->item->extends_explicit || state->item->fallback_explicit;
    return state->constraint_node || state->constraint || state->fallback;
}

// tsc's isConstTypeVariable: a `const` type parameter, or a type built on one that stays generic, where an argument's
// literals are checked as if under `as const`
bool ty_is_const_type_variable(Checker* me, Ty const* ty, u32 depth) {
    if (!ty || depth >= 5) return false;
    switch (ty->kind) {
        case TY_TYPE_PARAM: {
            while (ty->param.state->clone_of) ty = ty->param.state->clone_of;
            return ty->param.state->item && ty->param.state->item->is_const;
        }
        case TY_UNION:
        case TY_INTERSECTION:
            for (u32 i = 0; i < ty->composite.types.len; i++) {
                if (ty_is_const_type_variable(me, ty->composite.types.data[i], depth)) return true;
            }
            return false;
        case TY_INDEXED:
            return ty_is_const_type_variable(me, ty->indexed.object, depth + 1);
        default:
            return false;
    }
}

// tsc's someType(ty, isMutableArrayLikeType): whether some member is assignable to `any[]` (null: false)
static bool ty_some_mutable_array_like_at(Checker* me, Ty const* ty, u32 depth) {
    if (!ty) return false;
    if (ty->kind == TY_UNION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (ty_some_mutable_array_like_at(me, ty->composite.types.data[i], depth)) return true;
        }
        return false;
    }
    if (ty->flags & (TYF_NULL | TYF_UNDEFINED | TYF_ANY)) return (ty->flags & TYF_ANY) != 0;
    // (Of the apparent type: a type parameter's constraint)
    if (ty->kind == TY_TYPE_PARAM) return depth < 8 && ty_some_mutable_array_like_at(me, ty_param_constraint(me, ty), depth + 1);
    return ty_is_related(me, ty, ty_array(me, ty_intrinsic(me, TY_ANY), false), TY_RELATION_ASSIGNABLE);
}

bool ty_some_mutable_array_like(Checker* me, Ty const* ty) {
    return ty_some_mutable_array_like_at(me, ty, 0);
}

// The `this` type of a class or interface: constrained to the type as seen from inside (applied to its own type
// parameters)
Ty const* ty_this_type_param(Checker* me, Ty const* declared) {
    Ty const* ret = make_type_param(me, nullptr, checker_atom(me, c_sv("this")));
    TyParamState* state = ret->param.state;
    state->is_this = true;
    state->constraint = declared->declared.type_params.len ?
        ty_reference(me, declared, declared->declared.type_params) : declared;
    state->constraint_state = LAZY_STATE_DONE;
    return ret;
}

// References

static Ty const* normalized_tuple(Checker* me, Ty const* target, TyList args);

Ty const* ty_reference_with_this(Checker* me, Ty const* target, TyList args, Ty const* this_arg) {
    check(target->kind == TY_DECLARED);
    if (!target->declared.this_param) this_arg = nullptr;
    if (!target->declared.type_params.len && !this_arg) return target;
    check(args.len == target->declared.type_params.len);
    if (target->declared.kind == TY_DECLARED_TUPLE_TARGET) {
        Ty const* normalized = normalized_tuple(me, target, args);
        if (normalized) return normalized;
    }
    u64 stack_key[32];
    u32 count = 3 + args.len;
    u64* key = count <= c_arraylen(stack_key) ? stack_key : malloc(sizeof(u64) * count);
    key[0] = KEY_REFERENCE;
    key[1] = target->id;
    key[2] = this_arg ? this_arg->id : 0;
    for (u32 i = 0; i < args.len; i++) key[3 + i] = args.data[i]->id;
    void const** slot = intern_slot(me, &my types, key, count);
    if (key != stack_key) free(key);
    if (!*slot) {
        u32 flags = ty_list_flags(args) | (this_arg ? this_arg->flags : 0);
        // Arrays and tuples of fresh object literals widen element-wise (tsc's getWidenedType)
        bool widens = target == my array_target || target == my readonly_array_target ||
            target->declared.kind == TY_DECLARED_TUPLE_TARGET;
        u32 kept = TYF_COULD_CONTAIN_TYPE_VARIABLES | TYF_NON_INFERRABLE | (widens ? TYF_FRESH_OBJECT : 0);
        Ty* ret = ty_alloc(me, TY_REFERENCE, TYF_OBJECT | (flags & kept));
        ret->reference.target = target;
        ret->reference.args = ty_list_copy(me, args.data, args.len);
        ret->reference.this_arg = this_arg;
        ret->reference.state = arena_new(&my arena, TyObjectState);
        *slot = ret;
    }
    return *slot;
}

// A spread element whose type isn't known yet stays a spread (tsc's Variadic element)
static bool is_generic_spread(Checker* me, Ty const* ty) {
    if (ty->kind == TY_TYPE_PARAM || (ty->flags & TYF_DEFERRED)) return true;
    return ty->kind == TY_ANONYMOUS && (ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) && ty_members(me, ty)->mapped;
}

typedef struct TupleBuilder {
    Ty const** types;
    unsigned char* flags;
    Atom const** names;
    u32 len, cap;
    bool any_name;
    i32 last_required, first_rest, last_optional_or_rest;
} TupleBuilder;

static void tuple_builder_add(Checker* me, TupleBuilder* b, Ty const* type, unsigned char flags, Atom const* name) {
    if (b->len == b->cap) {
        b->cap = b->cap ? b->cap * 2 : 8;
        b->types = realloc(b->types, sizeof(Ty const*) * b->cap);
        b->flags = realloc(b->flags, b->cap);
        b->names = realloc(b->names, sizeof(Atom const*) * b->cap);
        panic_if(!b->types || !b->flags || !b->names, "out of memory");
    }
    bool rest = (flags & TUPLE_ELEMENT_REST) && !is_generic_spread(me, type);
    if (!(flags & (TUPLE_ELEMENT_OPTIONAL | TUPLE_ELEMENT_REST))) b->last_required = (i32)b->len;
    if (rest && b->first_rest < 0) b->first_rest = (i32)b->len;
    if ((flags & TUPLE_ELEMENT_OPTIONAL) || rest) b->last_optional_or_rest = (i32)b->len;
    b->types[b->len] = type;
    b->flags[b->len] = flags;
    b->names[b->len++] = name;
    b->any_name |= name != nullptr;
}

// tsc's createNormalizedTupleType: spreads of tuples are spliced in, a spread of a union is a union of tuples, and
// everything from the first rest element to the last optional or rest one is one rest element. Null if there's
// nothing to do
static Ty const* normalized_tuple(Checker* me, Ty const* target, TyList args) {
    TupleShape const* shape = target->declared.tuple;
    // A lone rest element of an array is that array (tsc's getTupleTargetType): `[...xs]` in a tuple context
    if (shape->arity == 1 && (shape->flags[0] & TUPLE_ELEMENT_REST) && ty_is_array(me, args.data[0])) {
        return shape->readonly ? ty_array(me, ty_reference_args(me, args.data[0]).data[0], true) : args.data[0];
    }
    bool needed = false;
    for (u32 i = 0; i < shape->arity && !needed; i++) {
        if (!(shape->flags[i] & TUPLE_ELEMENT_REST)) continue;
        Ty const* arg = args.data[i];
        needed = ty_is_tuple(arg) || (arg->kind == TY_DECLARED && arg->declared.kind == TY_DECLARED_TUPLE_TARGET) ||
            (arg->kind == TY_UNION && !(arg->flags & TYF_BOOLEAN)) || (arg->flags & TYF_NEVER);
    }
    if (!needed) return nullptr;
    for (u32 i = 0; i < shape->arity; i++) {
        Ty const* arg = args.data[i];
        if (!(shape->flags[i] & TUPLE_ELEMENT_REST) || !((arg->kind == TY_UNION && !(arg->flags & TYF_BOOLEAN)) || (arg->flags & TYF_NEVER))) {
            continue;
        }
        // [A, ...(X | Y)] is [A, ...X] | [A, ...Y]
        if (arg->flags & TYF_NEVER) return arg;
        Ty const** replaced = malloc(sizeof(Ty const*) * args.len);
        panic_if(!replaced, "out of memory");
        memcpy(replaced, args.data, sizeof(Ty const*) * args.len);
        TyBuf results = {0};
        for (u32 m = 0; m < arg->composite.types.len; m++) {
            replaced[i] = arg->composite.types.data[m];
            ty_buf_push(&results, ty_reference(me, target, (TyList){ .data = replaced, .len = args.len }));
        }
        Ty const* ret = ty_union(me, results.data, results.len, nullptr, (TyList){0});
        ty_buf_drop(&results);
        free(replaced);
        return ret;
    }
    TupleBuilder b = { .last_required = -1, .first_rest = -1, .last_optional_or_rest = -1 };
    for (u32 i = 0; i < shape->arity; i++) {
        Ty const* arg = args.data[i];
        Atom const* name = shape->names ? shape->names[i] : nullptr;
        if (!(shape->flags[i] & TUPLE_ELEMENT_REST)) {
            tuple_builder_add(me, &b, arg, shape->flags[i], name);
        } else if (ty_is_tuple(arg)) {
            TupleShape const* inner = arg->reference.target->declared.tuple;
            if (b.len + inner->arity >= 10000) {
                // TODO: TS2799 "Type produces a tuple type that is too large to represent." (TS2800 for expressions)
                free(b.types);
                free(b.flags);
                free(b.names);
                return ty_intrinsic(me, TY_ERROR);
            }
            TyList elements = ty_reference_args(me, arg);
            for (u32 j = 0; j < inner->arity; j++) {
                tuple_builder_add(me, &b, elements.data[j], inner->flags[j], inner->names ? inner->names[j] : nullptr);
            }
        } else if (!(arg->kind == TY_DECLARED && arg->declared.kind == TY_DECLARED_TUPLE_TARGET)) {
            tuple_builder_add(me, &b, arg, TUPLE_ELEMENT_REST, name);
        }
    }
    for (i32 i = 0; i < b.last_required; i++) b.flags[i] &= ~TUPLE_ELEMENT_OPTIONAL;
    if (b.first_rest >= 0 && b.first_rest < b.last_optional_or_rest) {
        TyBuf merged = {0};
        for (i32 i = b.first_rest; i <= b.last_optional_or_rest; i++) {
            Ty const* t = b.types[i];
            if (b.flags[i] & TUPLE_ELEMENT_REST) {
                t = is_generic_spread(me, t) ? ty_indexed(me, t, ty_intrinsic(me, TY_NUMBER)) : ty_rest_element_type(me, t);
            }
            ty_buf_push(&merged, t);
        }
        b.types[b.first_rest] = ty_array(me, ty_union(me, merged.data, merged.len, nullptr, (TyList){0}), false);
        b.flags[b.first_rest] = TUPLE_ELEMENT_REST;
        ty_buf_drop(&merged);
        u32 removed = (u32)(b.last_optional_or_rest - b.first_rest);
        for (u32 i = (u32)b.first_rest + 1; i + removed < b.len; i++) {
            b.types[i] = b.types[i + removed];
            b.flags[i] = b.flags[i + removed];
            b.names[i] = b.names[i + removed];
        }
        b.len -= removed;
    }
    TupleShape normalized = {
        .arity = b.len,
        .readonly = shape->readonly,
        .flags = b.flags,
        .names = b.any_name ? b.names : nullptr,
    };
    Ty const* new_target = ty_tuple_target(me, &normalized);
    Ty const* ret = b.len ? ty_reference(me, new_target, (TyList){ .data = b.types, .len = b.len }) : new_target;
    free(b.types);
    free(b.flags);
    free(b.names);
    return ret;
}

Ty const* ty_reference(Checker* me, Ty const* target, TyList args) {
    return ty_reference_with_this(me, target, args, nullptr);
}

Ty const* ty_with_this_arg(Checker* me, Ty const* ty, Ty const* this_arg) {
    if (ty == this_arg) return ty;
    if (ty->kind == TY_INTERSECTION) {
        TyBuf parts = {0};
        for (u32 i = 0; i < ty->composite.types.len; i++) ty_buf_push(&parts, ty_with_this_arg(me, ty->composite.types.data[i], this_arg));
        Ty const* ret = ty_intersection(me, parts.data, parts.len, nullptr, (TyList){0});
        ty_buf_drop(&parts);
        return ret;
    }
    if (ty->kind == TY_DECLARED && ty->declared.this_param) {
        return ty_reference_with_this(me, ty, ty->declared.type_params, this_arg);
    }
    if (ty->kind == TY_REFERENCE && !ty->reference.deferred && ty->reference.target->declared.this_param) {
        if (ty->reference.this_arg == this_arg) return ty;
        return ty_reference_with_this(me, ty->reference.target, ty_reference_args(me, ty), this_arg);
    }
    return ty;
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
    Symbol const* decl = program_get_global(my program, csview_from(name), SYMBOL_TYPE);
    if (decl && decl->flags & (SYMBOL_INTERFACE | SYMBOL_CLASS)) {
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
    // A lib without ReadonlyArray reads it as Array (tsc's getGlobalReadonlyArrayType)
    if (readonly && (*target)->declared.kind == TY_DECLARED_SYNTHETIC) return ty_array(me, element, false);
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

// tsc's getLiteralTypeFromProperty: a property declared with a numeric literal name (`{ 1: x }`) is keyed by the number
// literal, others by the name's string literal (or unique symbol)
Ty const* ty_prop_key_literal(Checker* me, Prop const* prop) {
    Prop const* declared = prop;
    while (declared->target && !declared->decl_span_count) declared = declared->target;
    csview text = declared->name_span.sv;
    if (declared->name_is_value && span_valid(declared->name_span) && text.size &&
        ((text.buf[0] >= '0' && text.buf[0] <= '9') || text.buf[0] == '.')
    ) {
        csview name = atom_sv(prop->name);
        char buf[64];
        if (name.size > 0 && name.size < (isize_t)sizeof(buf)) {
            memcpy(buf, name.buf, (size_t)name.size);
            buf[name.size] = 0;
            char* end = nullptr;
            double n = strtod(buf, &end);
            if (end == buf + name.size) {
                Value value = value_number(n);
                return ty_literal(me, &value);
            }
        }
    }
    return ty_prop_name_literal(me, prop->name);
}

Ty const* ty_prop_name_literal(Checker* me, Atom const* name) {
    Ty const* unique = ty_unique_symbol_of_name(me, name);
    if (unique) return unique;
    if (!ty_private_name_class(me, name)) return ty_string_literal(me, atom_sv(name));
    // A string literal of the private atom (keyed like literal_type(): the atom is none of its)
    u64 key[4] = { KEY_LITERAL, VALUE_STRING, (u64)(uintptr_t)name, 0 };
    void const** slot = intern_slot(me, &my types, key, 4);
    if (!*slot) {
        Ty* ret = ty_alloc(me, TY_LITERAL, TYF_STRING_LITERAL);
        ret->literal.value.tag = VALUE_STRING;
        ret->literal.value.string = cstr_null;
        ret->literal.string = name;
        *slot = ret;
    }
    return *slot;
}

bool ty_member_is_referenced(Checker* me, Span declaration) {
    u64 key[2] = { KEY_REFERENCED_MEMBER, (u64)(uintptr_t)declaration.sv.buf };
    return intern_get(&my links, key, 2) != nullptr;
}

void ty_mark_member_referenced(Checker* me, Span declaration) {
    if (!span_valid(declaration)) return;
    u64 key[2] = { KEY_REFERENCED_MEMBER, (u64)(uintptr_t)declaration.sv.buf };
    *intern_slot(me, &my links, key, 2) = me;
}

Atom const* ty_private_name(Checker* me, Symbol const* class_symbol, csview text) {
    Atom const* plain = checker_atom(me, text);
    u64 key[3] = { KEY_PRIVATE_NAME, (u64)(uintptr_t)class_symbol, (u64)(uintptr_t)plain };
    Atom const* found = intern_get(&my links, key, 3);
    if (found) return found;
    Atom* atom = arena_alloc(&my arena, sizeof(Atom) + (size_t)plain->len + 1);
    atom->len = plain->len;
    memcpy(atom->str, plain->str, plain->len);
    atom->str[plain->len] = 0;
    *intern_slot(me, &my links, key, 3) = atom;
    u64 back[2] = { KEY_PRIVATE_NAME_CLASS, (u64)(uintptr_t)atom };
    *intern_slot(me, &my links, back, 2) = class_symbol;
    return atom;
}

Symbol const* ty_private_name_class(Checker* me, Atom const* name) {
    if (!name->len || name->str[0] != '#') return nullptr;
    u64 key[2] = { KEY_PRIVATE_NAME_CLASS, (u64)(uintptr_t)name };
    return intern_get(&my links, key, 2);
}

Ty const* ty_unique_symbol_of_name(Checker* me, Atom const* name) {
    if (name->len < 4 || memcmp(name->str, "__@", 3) != 0) return nullptr;
    u64 key[2] = { KEY_UNIQUE_SYMBOL_NAME, (u64)(uintptr_t)name };
    return intern_get(&my links, key, 2);
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
    // `keyof (T & {})` (NonNullable<T>, LowInfer<T>) stays as it is, like in tsc's shouldDeferIndexType
    bool defer = false;
    if (operand->kind == TY_INTERSECTION && (operand->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) {
        bool has_variable = false, has_empty_object = false;
        for (u32 i = 0; i < operand->composite.types.len; i++) {
            Ty const* t = operand->composite.types.data[i];
            has_variable |= t->kind == TY_TYPE_PARAM || (t->flags & TYF_DEFERRED);
            if (t->kind == TY_ANONYMOUS) {
                Members const* members = ty_members(me, t);
                has_empty_object |= !members->prop_count && !members->call_count && !members->construct_count &&
                    !members->index_count && !members->mapped;
            }
        }
        defer = has_variable && has_empty_object;
    }
    if (operand->kind == TY_INTERSECTION && !defer) {
        TyBuf parts = {0};
        for (u32 i = 0; i < operand->composite.types.len; i++) {
            ty_buf_push(&parts, ty_keyof(me, operand->composite.types.data[i]));
        }
        Ty const* ret = ty_union(me, parts.data, parts.len, nullptr, (TyList){0});
        ty_buf_drop(&parts);
        return ret;
    }
    // The property names of `List<T>` or `{ a: T }` don't depend on T: only type variables and generic mapped types
    // (members->mapped) defer, like tsc's isGenericObjectType
    bool known_names = !ty_is_tuple(operand) || !(operand->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES);
    if (!defer && (operand->flags & TYF_OBJECT) && known_names) {
        Members const* members = ty_members(me, operand);
        // Of a mapped type without an `as` clause, its constraint (tsc's getIndexTypeForMappedType):
        // `keyof { [P in string]: X }` is string, `keyof Pick<T, unknown>` unknown
        if (members->expanded_from && !members->expanded_from->name_type) return members->expanded_from->constraint;
        if (members->mapped && !members->mapped->name_type) return members->mapped->constraint;
        if (!members->mapped) {
            TyBuf parts = {0};
            for (u32 i = 0; i < members->prop_count; i++) {
                if (ty_private_name_class(me, members->props[i]->name)) continue; // Not a key (tsc's getLiteralTypeFromProperty)
                ty_buf_push(&parts, ty_prop_key_literal(me, members->props[i]));
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

// tsc's isGenericTupleType: a tuple with a variadic element, a spread of something generic (`[...T]`, not `[...T[]]`).
// A deferred reference whose arguments aren't resolved yet goes by its syntax, like tsc's tuple target does
bool ty_is_generic_tuple(Checker* me, Ty const* ty) {
    if (!(ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) || !ty_is_tuple(ty)) return false;
    TyDeferredRef const* deferred = ty->reference.deferred;
    if (deferred && deferred->state != LAZY_STATE_DONE) {
        return deferred->node->tag == TYPE_TUPLE && ty_tuple_node_has_variadic(deferred->node);
    }
    TupleShape const* shape = ty_tuple_shape(ty);
    TyList args = ty_reference_args(me, ty);
    for (u32 i = 0; i < shape->arity && i < args.len; i++) {
        if (!(shape->flags[i] & TUPLE_ELEMENT_REST)) continue;
        Ty const* arg = args.data[i];
        if (arg->kind == TY_TYPE_PARAM || (arg->flags & TYF_DEFERRED)) return true;
    }
    return false;
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
    return ty_is_generic_tuple(me, ty);
}

// The access itself is ty_access() (access.c), the same lookup property access expressions get
static Ty const* try_resolve_indexed(Checker* me, Ty const* object, Ty const* index) {
    if ((object->flags & TYF_ANY) || (index->flags & TYF_ERROR)) return object->flags & TYF_ANY ? object : index;
    if (index->flags & TYF_NEVER) return index;
    if (is_generic_operand(me, object, 0) || is_generic_operand(me, index, 0)) return nullptr;
    return ty_access(me, object, index);
}

Ty const* ty_indexed(Checker* me, Ty const* object, Ty const* index) {
    index = ty_regular(me, index);
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

// What an instantiation makes of `O[K]`: like ty_indexed(), but a non-generic access that finds nothing is unknown
// (tsc's getIndexedAccessType without an access node; the error was the declaration's to report): falso's
// `O['length'] extends number ? T[] : T` with `O = { withAccents: false }` takes the false branch
Ty const* ty_indexed_instantiated(Checker* me, Ty const* object, Ty const* index) {
    Ty const* ret = ty_indexed(me, object, index);
    if (ret->kind != TY_INDEXED || is_generic_operand(me, object, 0) || is_generic_operand(me, index, 0)) return ret;
    // (Only if it is this access that stayed deferred: a property whose type is an indexed access, `{ a: U["x"] }["a"]`,
    // resolved fine)
    if (is_generic_operand(me, ret->indexed.object, 0) || is_generic_operand(me, ret->indexed.index, 0)) return ret;
    return my intrinsics[TY_UNKNOWN_];
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
    Ty const* ret = ty_union_filtered(me, ty, kept, len);
    free(kept);
    return ret;
}

Ty const* ty_without_undefined(Checker* me, Ty const* type) {
    if (type->kind != TY_UNION) return type->flags & TYF_UNDEFINED ? ty_intrinsic(me, TY_NEVER) : type;
    TyList members = type->composite.types;
    Ty const** kept = malloc(sizeof(Ty const*) * members.len);
    panic_if(!kept, "out of memory");
    u32 len = 0;
    for (u32 i = 0; i < members.len; i++) {
        if (!(members.data[i]->flags & TYF_UNDEFINED)) kept[len++] = members.data[i];
    }
    Ty const* ret = ty_union_filtered(me, type, kept, len);
    free(kept);
    return ret;
}

Ty const* ty_cond_branch_type(Checker* me, TyCondBranch* branch) {
    if ((!branch->node && !branch->source) || branch->state == LAZY_STATE_DONE) return branch->type;
    if (branch->state == LAZY_STATE_IN_FLIGHT) return ty_intrinsic(me, TY_ERROR);
    branch->state = LAZY_STATE_IN_FLIGHT;
    Ty const* source = branch->node ? ty_from_node(me, branch->node, branch->ctx) : branch->source;
    branch->type = ty_instantiate_tail(me, source, branch->mapper);
    branch->state = LAZY_STATE_DONE;
    return branch->type;
}

static u32 cond_branch_key_len(TyCondBranch const* branch) {
    if (!branch->node && !branch->source) return 2;
    return 2 + (branch->mapper ? branch->mapper->len * 2 : 0);
}

static u32 cond_branch_key(u64* key, TyCondBranch const* branch) {
    if (!branch->node && !branch->source) {
        key[0] = branch->type->id;
        key[1] = 0;
        return 2;
    }
    u32 count = 0;
    key[count++] = branch->node ? (u64)(uintptr_t)branch->node : (u64)(uintptr_t)branch->source;
    key[count++] = branch->mapper ? branch->mapper->len : 0;
    for (u32 i = 0; branch->mapper && i < branch->mapper->len; i++) {
        key[count++] = branch->mapper->sources[i]->id;
        key[count++] = branch->mapper->targets[i]->id;
    }
    return count;
}

// Resolves right away unless the check type is generic (conditional.c)
Ty const* ty_conditional(
    Checker* me, Ty const* check_type, Ty const* extends, TyCondBranch const* if_true, TyCondBranch const* if_false,
    Ty const* root, TyMapper const* mapper, bool distributive, TyList infers
) {
    Ty const* resolved = ty_conditional_resolve(me, check_type, extends, infers, if_true, if_false);
    if (resolved) return resolved;
    u64 stack_key[64];
    u32 key_len = 5 + cond_branch_key_len(if_true) + cond_branch_key_len(if_false);
    u64* key = key_len <= c_arraylen(stack_key) ? stack_key : malloc(sizeof(u64) * key_len);
    panic_if(!key, "out of memory");
    key[0] = KEY_CONDITIONAL;
    key[1] = check_type->id;
    key[2] = extends->id;
    key[3] = distributive;
    key[4] = root ? root->id : 0;
    u32 count = 5;
    count += cond_branch_key(key + count, if_true);
    count += cond_branch_key(key + count, if_false);
    u32 flags = check_type->flags | extends->flags | TYF_COULD_CONTAIN_TYPE_VARIABLES;
    Ty* fresh;
    Ty const* ret = intern_deferred(me, key, count, TY_CONDITIONAL, flags, &fresh);
    if (key != stack_key) free(key);
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
