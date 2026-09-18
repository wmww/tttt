# Generics
Cleat ships with [STC](https://github.com/stclib/STC) vendored under [stc/](../stc/). STC's generics are wrapped by cleat to add features such as [more complex iterators](iterators_and_for_each.md) and allow method names and type names to be differently cased.

**This document was written by Claude Opus 4.7 and human-reviewed, it may be of lower quality than the human-written documentation**

## Basic usage
Like STC, a container header is #included once for each type instantiation, and #defines above this inclusion are used to define the type:
```c
#include <stc/cstr.h>
#include <cleat/types.h>

#define g_pro_key CStr, cstr
#include <cleat/generic/vec.h>
// VecCStr type and vec_cstr_*() functions are now defined
```

`g_*` macros are used instead of STC's `i_*` macros, but they otherwise match the STC usage (eg `g_key`, `g_class_key`, `g_pro_key`, `g_val`, `g_class_val`, `g_pro_val`, `g_eq`, `g_cmp`, `g_hash`, `g_opt`).

The wrapped containers live under `cleat/generic` and include most of the common STC containers (`vec`, `hmap`, `smap`, `hset`, `sset`, `box`, `arc`).

The simplest container of `int`s:
```c
#include <cleat/types.h>
#include <cleat/generic/types.h>

typedef int Int;
#define g_key Int, int
#include <cleat/generic/vec.h>

void use(void) {
    VecInt v = vec_int_init();
    vec_int_push(&v, 42);
    vec_int_drop(&v);
}
```
The pair after `g_key` is `<type-name>, <function-prefix>`. Type names are generally `PascalCase` and prefix is generally `snake_case`.

## Class types — drop and clone
For a type `T` with matching `t_drop` and `t_clone` functions, use `g_class_key` to bind both at once:
```c
#include <stc/cstr.h>
#include <cleat/types.h>
#include <cleat/generic/types.h>

typedef struct Person Person;
struct Person {
    cstr name;
    int age;
};
static void person_drop(Person* p) { cstr_drop(&p->name); }
static Person person_clone(Person p) { p.name = cstr_clone(p.name); return p; }

#define g_class_key Person, person
#include <cleat/generic/vec.h>

void use(void) {
    VecPerson v = vec_person_init();
    vec_person_push(&v, (Person){.name = cstr_lit("Alice"), .age = 30});
    VecPerson w = vec_person_clone(v);
    vec_person_drop(&v);
    vec_person_drop(&w);
}
```
Pair with `g_no_clone` if your type has a `_drop` but no `_clone`:
```c
#include <cleat/types.h>
#include <cleat/generic/types.h>

typedef struct Handle Handle;
struct Handle { int fd; };
static void handle_drop(Handle* h) { (void)h; /* close(h->fd) */ }

#define g_class_key Handle, handle
#define g_no_clone
#include <cleat/generic/vec.h>
```

## Pro types — cstr, box, arc
"Pro" types have a richer convention: in addition to `_drop` and `_clone` they expose a `_raw` lookup type plus `_cmp` / `_eq` / `_hash`. `cstr`, `BoxX` and `ArcX` are the common ones; use them through `g_pro_key` (or `g_pro_val`):
```c
#include <stc/cstr.h>
#include <cleat/types.h>

#define g_pro_key CStr, cstr
#include <cleat/generic/vec.h>

// Or:
// #define g_pro_key SvStr, svstr
// For csview as the raw type instead of const char*

void use(void) {
    VecCStr v = vec_cstr_init();
    vec_cstr_push(&v, cstr_lit("hello"));
    vec_cstr_drop(&v);
}
```

## Custom names with g_type
The defaults are `Vec<KeyType>`, `HMap<ValType>`, `HSet<KeyType>`, etc. Override both names at once with `g_type`:
```c
#include <stc/cstr.h>
#include <cleat/types.h>

#define g_type Counters, counters
#define g_pro_key CStr, cstr
#define g_val int, int
#include <cleat/generic/hmap.h>

void use(void) {
    Counters c = counters_init();
    counters_insert(&c, cstr_lit("a"), 1);
    counters_drop(&c);
}
```

## Maps — keys and values
Maps (`HMap`, `SMap`) need both a key and a value. Use `g_key` / `g_class_key` / `g_pro_key` for the key and the matching `g_val` / `g_class_val` / `g_pro_val` for the value. By default the type name comes from the value alone:
```c
#include <stc/cstr.h>
#include <cleat/types.h>

typedef int Int;
#define g_pro_key CStr, cstr
#define g_val Int, int
#include <cleat/generic/hmap.h>

void use(void) {
    HMapInt m = hmap_int_init();
    hmap_int_insert(&m, cstr_lit("answer"), 42);
    hmap_int_drop(&m);
}
```
If two maps would collide on that name (e.g. `<int, int>` and `<cstr, int>` both becoming `HMapInt`), set `g_include_key` to fold the key into the name:
```c
#include <stc/cstr.h>
#include <cleat/types.h>

typedef int Int;
#define g_pro_key CStr, cstr
#define g_val Int, int
#define g_include_key
#include <cleat/generic/hmap.h>

void use(void) {
    HMapCStrInt m = hmap_cstr_int_init();
    hmap_cstr_int_insert(&m, cstr_lit("answer"), 42);
    hmap_cstr_int_drop(&m);
}
```
Or just use `g_type` to name it whatever you want.

## Forward declarations
Containers are usually expanded inline into a struct, but if you need just the typedef and forward declaration (e.g. for a header that two `.c` files share, or because the contained type isn't visible yet), [generic/types.h](../include/cleat/generic/types.h) provides `g_declare_*` macros:
```c
#include <cleat/types.h>
#include <cleat/generic/types.h>

typedef struct Node Node;
g_declare_vec(Node, node);
g_declare_hmap(cstr, Node, node);

// Now VecNode, vec_node, HMapNode, hmap_node are typedef'd, but the struct is incomplete
// and the functions are not yet defined.
```
At the point you actually expand the container, set `g_declared` so the wrapper skips re-emitting the typedef:
```c
#include <stc/cstr.h>
#include <cleat/types.h>
#include <cleat/generic/types.h>

typedef struct Node Node;
g_declare_vec(Node, node);

struct Node { int id; };
static void node_drop(Node* n) { (void)n; }

#define g_class_key Node, node
#define g_declared
#define g_no_clone
#include <cleat/generic/vec.h>
```

## Splitting declarations and implementation
A container expanded in a header would normally produce a multiple-definition error if the header is included from more than one `.c` file. `g_header` emits declarations only:
```c
// in mylib.h:
#include <stc/cstr.h>
#include <cleat/types.h>

#define g_pro_key CStr, cstr
#define g_header
#include <cleat/generic/vec.h>
```
Then in exactly one `.c` file, define `g_implement` before any include that pulls in that header. `g_implement` is sticky — it isn't undef'd between includes — so a single define at the top of the file covers every `g_header` container reachable from your headers. [string_helpers.h](../include/cleat/string_helpers.h) and [string_helpers.c](../src/string_helpers.c) are the canonical example.

## Other knobs
- `g_eq(a, b)`, `g_cmp(a, b)`, `g_hash(p)` — supply explicit comparison or hashing for the key. Useful when a class type doesn't already provide them, or when you want different semantics than the default.
- `g_opt (...)` — passed straight through to STC's `i_opt`; e.g. `#define g_opt (c_use_cmp)` enables `_sort` / `_lower_bound` / `_binary_search` on a vec whose key is integral.
- `g_display` — synthesises a display function for the container (used by the `display.h` formatting system). Available for `vec`, `hmap`, `smap`, `hset`, `sset`, `box`, `arc`.

## Iterating
Every container exported by these wrappers has an `_iter_deref()` function generated for it, so `for_each` and `for_each_kv` from [common_macros.h](../include/cleat/common_macros.h) Just Work:
```c
#include <stc/cstr.h>
#include <cleat/common_macros.h>
typedef int Int;
#define g_pro_key CStr, cstr
#define g_val Int, int
#include <cleat/generic/smap.h>

void use(SMapInt const* m) {
    for_each_kv (k, v, smap_int, m) {
        (void)k; (void)v; // *k is cstr, *v is int
    }
}
```
See [iterators and for_each](iterators_and_for_each.md) for the iteration model.

## Containers and methods
Each cleat header is a thin wrapper around the STC header of the same name. The STC API docs cover every method; these are the headers and a representative slice of what they expose. In the STC docs `X` is the generic suffix — for cleat, that's the second half of your `g_key` / `g_val` / `g_type` (i.e. what gets concatenated into the function prefix).

| cleat header | STC base | STC docs | Notes |
|:---|:---|:---|:---|
| [generic/vec.h](../include/cleat/generic/vec.h) | [stc/vec.h](../stc/include/stc/vec.h) | [vec_api.md](../stc/docs/vec_api.md) | Dynamic array. |
| [generic/hmap.h](../include/cleat/generic/hmap.h) | [stc/hmap.h](../stc/include/stc/hmap.h) | [hmap_api.md](../stc/docs/hmap_api.md) | Hash map. |
| [generic/smap.h](../include/cleat/generic/smap.h) | [stc/smap.h](../stc/include/stc/smap.h) | [smap_api.md](../stc/docs/smap_api.md) | Sorted map (B-tree). |
| [generic/hset.h](../include/cleat/generic/hset.h) | [stc/hset.h](../stc/include/stc/hset.h) | [hset_api.md](../stc/docs/hset_api.md) | Hash set. |
| [generic/sset.h](../include/cleat/generic/sset.h) | [stc/sset.h](../stc/include/stc/sset.h) | [sset_api.md](../stc/docs/sset_api.md) | Sorted set. |
| [generic/box.h](../include/cleat/generic/box.h) | [stc/box.h](../stc/include/stc/box.h) | [box_api.md](../stc/docs/box_api.md) | Heap pointer with single ownership (like `unique_ptr`). |
| [generic/arc.h](../include/cleat/generic/arc.h) | [stc/arc.h](../stc/include/stc/arc.h) | [arc_api.md](../stc/docs/arc_api.md) | Atomically reference-counted heap pointer (like `shared_ptr`). |

Here is a reference for convenience, see the STC docs for more info.

**Vec** (`vec_X_*`) — dynamic array of `T`
- `VecX vec_X_init(void)` — empty vec
- `VecX vec_X_with_capacity(isize size)` — empty vec preallocated for `size` elements
- `VecX vec_X_with_size(isize size, T null)` — vec of `size` copies of `null`
- `VecX vec_X_clone(VecX vec)` — deep copy
- `void vec_X_take(VecX* self, VecX unowned)` — take ownership of `unowned`
- `VecX vec_X_move(VecX* self)` — transfer ownership out, leaves `self` empty
- `void vec_X_drop(VecX const* self)` — destruct
- `void vec_X_clear(VecX* self)` — drop every element, keep capacity
- `bool vec_X_reserve(VecX* self, isize cap)` — ensure capacity ≥ `cap`
- `bool vec_X_resize(VecX* self, isize size, T null)` — grow with copies of `null` or shrink
- `void vec_X_shrink_to_fit(VecX* self)` — release unused capacity
- `bool vec_X_is_empty(VecX const* self)`
- `isize vec_X_size(VecX const* self)`
- `isize vec_X_capacity(VecX const* self)`
- `T const* vec_X_at(VecX const* self, isize idx)` / `T* vec_X_at_mut(VecX*, isize)`
- `T const* vec_X_front(VecX const* self)` / `T* vec_X_front_mut(VecX*)`
- `T const* vec_X_back(VecX const* self)` / `T* vec_X_back_mut(VecX*)`
- `T* vec_X_push(VecX* self, T value)` — append, returns pointer to new element
- `void vec_X_pop(VecX* self)` — drop the last element
- `T vec_X_pull(VecX* self)` — move out the last element
- `vec_X_iter vec_X_insert_at(VecX* self, vec_X_iter it, T value)` — insert before `it`
- `vec_X_iter vec_X_insert_n(VecX* self, isize idx, T const arr[], isize n)` — insert `n` values at `idx`
- `vec_X_iter vec_X_erase_at(VecX* self, vec_X_iter it)` — remove element at `it`
- `vec_X_iter vec_X_erase_n(VecX* self, isize idx, isize n)` — remove `n` elements starting at `idx`
- `vec_X_iter vec_X_erase_range(VecX*, vec_X_iter it1, vec_X_iter it2)` — remove `[it1, it2)`
- `vec_X_iter vec_X_begin(VecX const* self)` / `vec_X_iter vec_X_end(VecX const*)` / `void vec_X_next(vec_X_iter*)` / `T* vec_X_iter_deref(vec_X_iter*)`
- With `g_opt (c_use_cmp)` (or `g_cmp` / `g_less`):
  - `void vec_X_sort(VecX* self)` — quicksort in place
  - `vec_X_iter vec_X_find(VecX const* self, T raw)` — linear search
  - `isize vec_X_lower_bound(VecX const* self, T raw)` — first index ≥ `raw` in a sorted vec, or `c_NPOS`
  - `isize vec_X_binary_search(VecX const* self, T raw)` — index of `raw` in a sorted vec, or `c_NPOS`

**HMap** (`hmap_X_*`) — hash map of `K → V`
- `HMapX hmap_X_init(void)`
- `HMapX hmap_X_with_capacity(isize cap)`
- `HMapX hmap_X_clone(HMapX map)` / `void hmap_X_take(HMapX*, HMapX unowned)` / `HMapX hmap_X_move(HMapX*)` / `void hmap_X_drop(HMapX const*)`
- `void hmap_X_clear(HMapX* self)`
- `bool hmap_X_reserve(HMapX* self, isize size)` / `void hmap_X_shrink_to_fit(HMapX*)`
- `float hmap_X_max_load_factor(HMapX const* self)`
- `bool hmap_X_is_empty(HMapX const* self)` / `isize hmap_X_size(HMapX const*)` / `isize hmap_X_capacity(HMapX const*)` / `isize hmap_X_bucket_count(HMapX const*)`
- `V const* hmap_X_at(HMapX const* self, K rkey)` — value for `rkey`; key must exist
- `V* hmap_X_at_mut(HMapX* self, K rkey)` — mutable variant
- `hmap_X_value const* hmap_X_get(HMapX const* self, K rkey)` — `{first, second}` pair, or `nullptr`
- `hmap_X_value* hmap_X_get_mut(HMapX* self, K rkey)`
- `bool hmap_X_contains(HMapX const* self, K rkey)`
- `hmap_X_iter hmap_X_find(HMapX const* self, K rkey)` — `.ref == nullptr` when not found
- `hmap_X_result hmap_X_insert(HMapX* self, K key, V mapped)` — no change if key already present; result is `{ value*, inserted }`
- `hmap_X_result hmap_X_insert_or_assign(HMapX* self, K key, V mapped)` — always overwrites
- `hmap_X_result hmap_X_put(HMapX* self, K rkey, V rmapped)` — alias for `_insert_or_assign`
- `hmap_X_result hmap_X_push(HMapX* self, hmap_X_value entry)` — assign a key/value pair
- `int hmap_X_erase(HMapX* self, K rkey)` — returns 0 or 1
- `hmap_X_iter hmap_X_erase_at(HMapX* self, hmap_X_iter it)` — returns the next iter
- `void hmap_X_erase_entry(HMapX* self, hmap_X_value* entry)`
- `hmap_X_iter hmap_X_begin(HMapX const* self)` / `hmap_X_iter hmap_X_end(HMapX const*)` / `void hmap_X_next(hmap_X_iter*)` / `hmap_X_value* hmap_X_iter_deref(hmap_X_iter*)`
- Iterators dereference to `hmap_X_value`, which is `struct { K const first; V second; }` — use `for_each_kv` to bind `first` and `second` directly

**SMap** (`smap_X_*`) — sorted map of `K → V` (B-tree). Same shape as `hmap` plus the order-aware methods:
- `smap_X_init` / `_with_capacity` / `_clone` / `_take` / `_move` / `_drop` / `_clear` / `_reserve` / `_shrink_to_fit`
- `smap_X_is_empty` / `_size` / `_capacity`
- `smap_X_at` / `_at_mut` / `_get` / `_get_mut` / `_contains` / `_find`
- `K const* smap_X_find_it(SMapX const* self, K rkey, smap_X_iter* out)` — returns `nullptr` if not found, fills `out` with the iterator
- `smap_X_iter smap_X_lower_bound(SMapX const* self, K rkey)` — first entry with key ≥ `rkey`
- `K const* smap_X_front(SMapX const* self)` / `K const* smap_X_back(SMapX const*)` — smallest / largest key
- `smap_X_insert` / `_insert_or_assign` / `_put` / `_push` (same semantics as `hmap`)
- `smap_X_erase` / `_erase_at`
- `smap_X_iter smap_X_erase_range(SMapX* self, smap_X_iter it1, smap_X_iter it2)`
- `smap_X_begin` / `_end` / `_next` / `_iter_deref`

**HSet** (`hset_X_*`) — hash set of `T`
- `HSetX hset_X_init(void)` / `_with_capacity` / `_clone` / `_take` / `_move` / `_drop` / `_clear` / `_reserve` / `_shrink_to_fit`
- `float hset_X_max_load_factor(HSetX const* self)` — default 0.85
- `hset_X_is_empty` / `_size` / `_capacity` / `_bucket_count`
- `bool hset_X_contains(HSetX const* self, T rkey)`
- `T const* hset_X_get(HSetX const* self, T rkey)` — `nullptr` when not found
- `T* hset_X_get_mut(HSetX* self, T rkey)`
- `hset_X_iter hset_X_find(HSetX const* self, T rkey)`
- `hset_X_result hset_X_insert(HSetX* self, T key)` — no-op if already present; result is `{ value*, inserted }`
- `hset_X_result hset_X_push(HSetX* self, T key)` — alias for `_insert`
- `hset_X_result hset_X_put(HSetX* self, T rkey)` — like emplace, also works when key/raw types match
- `int hset_X_erase(HSetX* self, T rkey)` — returns 0 or 1
- `hset_X_iter hset_X_erase_at(HSetX* self, hset_X_iter it)`
- `void hset_X_erase_entry(HSetX* self, T* entry)`
- `hset_X_begin` / `_end` / `_next` / `_iter_deref`

**SSet** (`sset_X_*`) — sorted set of `T` (B-tree). Same shape as `hset`:
- `sset_X_init` / `_with_capacity` / `_clone` / `_take` / `_move` / `_drop` / `_clear` / `_reserve` / `_shrink_to_fit`
- `sset_X_is_empty` / `_size` / `_capacity`
- `sset_X_get` / `_get_mut` / `_contains` / `_find`
- `T const* sset_X_find_it(SSetX const* self, T rkey, sset_X_iter* out)`
- `sset_X_iter sset_X_lower_bound(SSetX const* self, T rkey)` — first element ≥ `rkey`
- `sset_X_insert` / `_push` / `_put` (same semantics as `hset`)
- `sset_X_erase` / `_erase_at` / `_erase_range`
- `sset_X_begin` / `_end` / `_next` / `_iter_deref`

**Box** (`box_X_*`) — single-owner heap pointer to `T`. The contained value is reached through the `.get` field.
- `BoxX box_X_init(void)` — empty (`.get == nullptr`)
- `BoxX box_X_make(T val)` — heap-allocate and move `val` in
- `BoxX box_X_from_ptr(T* ptr)` — take ownership of an already-allocated `ptr`
- `BoxX box_X_clone(BoxX other)` — deep copy (uses `T_clone`)
- `void box_X_assign(BoxX* self, BoxX* other)` — transfer ownership from `other`; sets `other` to `nullptr`
- `void box_X_take(BoxX* self, BoxX unowned)` — drop `self`, take `unowned`
- `BoxX box_X_move(BoxX* self)` — transfer ownership out; sets `self` to `nullptr`
- `T* box_X_release(BoxX* self)` — give up ownership of the underlying pointer (caller must free)
- `void box_X_reset_to(BoxX* self, T* ptr)` — drop the current value and adopt `ptr`
- `void box_X_drop(BoxX const* self)` — destruct value and free
- `int box_X_cmp(BoxX const* x, BoxX const* y)` / `bool box_X_eq(...)` / `size_t box_X_hash(...)` — pointer-compare unless `g_cmp` is set

**Arc** (`arc_X_*`) — atomically reference-counted shared pointer to `T`. Like `Box`, the value is reached through `.get`. Cloning bumps the count, dropping decrements; the value is destructed when the last `Arc` is dropped.
- `ArcX arc_X_init(void)` — empty
- `ArcX arc_X_make(T val)` — allocate, move in, refcount = 1
- `ArcX arc_X_clone(ArcX other)` — bump the refcount, return another handle
- `void arc_X_assign(ArcX* self, ArcX const* other)` — shared assign (bumps count)
- `void arc_X_take(ArcX* self, ArcX unowned)` — drop `self`, adopt `unowned`
- `ArcX arc_X_move(ArcX* self)` — transfer the handle out; sets `self` to `nullptr`
- `void arc_X_drop(ArcX const* self)` — decrement; destructs and frees at zero
- `long arc_X_use_count(ArcX arc)` — current refcount
- `int arc_X_cmp(ArcX const* x, ArcX const* y)` / `bool arc_X_eq(...)` / `size_t arc_X_hash(...)` — pointer-compare unless `g_cmp` is set
