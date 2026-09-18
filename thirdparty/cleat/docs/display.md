# Display
[display.h](../include/cleat/display.h) is an efficient, ergonomic and extendable printing/formatting system. It replaces functions like `printf`, `sprintf`, and STC's `cstr_fmt` with a format string-free approach based on the `_Generic` C feature.

**This document was written by GPT 5.5 and human-reviewed, it may be of lower quality than the human-written documentation**

```c
#include <cleat/display.h>

void demo(i32 id, csview name) {
    println(id, ": my name is ", name);
}
```

## Basic output
- `cstr_d(arg1, arg2, ...)` returns a fresh `cstr`.
- `print(...)` and `println(...)` write to stdout.
- `eprint(...)` and `eprintln(...)` write to stderr.

```c
#include <cleat/display.h>

void use() {
    cstr s = cstr_d("count = ", 7, ", ok = ", true);
    cstr_drop(&s);

    println("hello, ", "world!");
    eprintln("oops: ", 42);
}
```

Each argument must have an overload in the `DISPLAYABLES` macro that is visible at the call site. Out of the box, `DISPLAYABLES` is `CORE_DISPLAYABLES`.

`CORE_DISPLAYABLES` supports:
- strings: `char const*`, `char*`, `csview`, `cstr`
- primitive types: `i32`, `u32`, `i64`, `u64`, `float`, `double`, `char`, `bool`, `void*`
- display helpers: `nl` (newline), `nl_or_space`, `consume_d(X)`, `debug_d(X)`
- errors: `ErrorBase const*`

Strings are appended as-is by default. Wrap a string-like value in `debug_d(...)` to display it with quotes and C-style escapes:

```c
#include <cleat/display.h>

void use() {
    cstr name = cstr_from("Ada\nLovelace");
    cstr raw = cstr_d("name = ", name);
    cstr debug = cstr_d("name = ", debug_d(name));

    cstr_drop(&raw);
    cstr_drop(&debug);
    cstr_drop(&name);
}
```

In that example, `raw` contains the actual newline, while `debug` contains `name = "Ada\nLovelace"`.

## Adding overloads
`display_sequence`, `cstr_d`, `print`, and friends dispatch with C11 `_Generic`. To make a type displayable in a `.c` file, redefine `DISPLAYABLES` in that file:

```c
#include <cleat/display.h>

typedef struct Point Point;
struct Point {
    i32 x;
    i32 y;
};

impl_display_no_drop(Point, point, static) {
    return display_sequence(d, "(", me.x, ", ", me.y, ")");
}

#undef DISPLAYABLES
#define DISPLAYABLES \
    CORE_DISPLAYABLES, \
    Point: point_display

void use() {
    Point p = {.x = 3, .y = 4};
    cstr s = cstr_d("p = ", p);
    cstr_drop(&s);
}
```

`impl_display_no_drop(Type, prefix, qualifiers)` opens the body of:

```text
DisplayCtx example_display(Example me, DisplayCtx d);
```

The helper macro writes the real function signature for you. The optional third argument is usually `static` or `static inline`; omit it for an external definition.

Each extra entry is written as `Type: display_function`.

## Owned values and `consume_d`
Display functions receive values by value:

```text
DisplayCtx point_display(Point me, DisplayCtx d);
```

For plain types, that is all you need. For owning types with a matching `<prefix>_drop` function, use `impl_display_class`. It still exposes the same public display function, but the body receives a borrowed pointer to the copied value:

```c
#include <cleat/display.h>

typedef struct Person Person;
struct Person {
    cstr name;
    i32 age;
};

static void person_drop(Person* me) {
    cstr_drop(&me->name);
}

impl_display_class(Person, person, static) {
    return display_sequence(d, "Person(", me->name, ", age=", me->age, ")");
}

#undef DISPLAYABLES
#define DISPLAYABLES \
    CORE_DISPLAYABLES, \
    Person: person_display

static Person make_person() {
    return (Person){.name = cstr_lit("Bob"), .age = 25};
}

void use(Person const* borrowed) {
    cstr a = cstr_d(*borrowed);

    Person owned = make_person();
    cstr b = cstr_d(consume_d(owned));

    cstr_drop(&a);
    cstr_drop(&b);
}
```

`cstr_d(*borrowed)` displays a copy and does not call `person_drop`. Wrapping a value in `consume_d(...)` sets `DisplayCtx.drop_after_display` for that one argument, so `impl_display_class` calls `person_drop` after rendering it.

`debug_d(...)` is also scoped to one argument. It changes how string-like displays render that argument without consuming it:

```c
#include <cleat/display.h>

void use() {
    cstr s = cstr_from("line 1\nline 2");
    cstr raw = cstr_d(s);
    cstr debug = cstr_d(debug_d(s));

    cstr_drop(&raw);
    cstr_drop(&debug);
    cstr_drop(&s);
}
```

Use `consume_d(...)` for temporary owning values that would otherwise need a manual drop:

```c
#include <cleat/display.h>

void use() {
    cstr s = cstr_d("tmp = ", consume_d(cstr_from("temporary")));
    cstr_drop(&s);
}
```

Do not drop or otherwise use a value after passing it to `consume_d(...)`; its owned contents have been consumed.

If you want to put a display declaration in a header and the implementation in a `.c` file, use `declare_display(Type, prefix)` in the header and the matching `impl_display_no_drop` or `impl_display_class` without `static` in exactly one `.c` file.

## Generic containers
Container instantiations can define display functions with `g_display`. Add the generated container type to `DISPLAYABLES` where you want to call `cstr_d` or `print` on it:

```c
#include <cleat/display.h>

#define g_type Scores, scores
#define g_key i32, i32
#define g_display
#include <cleat/generic/vec.h>

#undef DISPLAYABLES
#define DISPLAYABLES \
    CORE_DISPLAYABLES, \
    Scores: scores_display

void use() {
    Scores scores = scores_init();
    scores_push(&scores, 10);
    scores_push(&scores, 20);
    scores_push(&scores, 30);

    cstr compact = cstr_d(scores);
    cstr expanded = cstr_d(((DisplayCtx){.multiline = true}), scores);

    cstr_drop(&compact);
    cstr_drop(&expanded);
    scores_drop(&scores);
}
```

`g_display` is available for `vec`, `hmap`, `smap`, `hset`, `sset`, `box`, and `arc`. The generated display function calls the element, key, or value display function by prefix. For example, `g_key i32, i32` uses `i32_display`. Container displays temporarily enable debug display for their elements, so string elements are quoted and escaped inside collections.

[string_helpers.h](../include/cleat/string_helpers.h) predefines displayable string collections. Include it and append `STRING_COLLECTION_DISPLAYABLES`:

```c
#include <cleat/display.h>
#include <cleat/string_helpers.h>

#undef DISPLAYABLES
#define DISPLAYABLES \
    CORE_DISPLAYABLES, \
    STRING_COLLECTION_DISPLAYABLES

void use(VecCStr const* strings) {
    cstr s = cstr_d(*strings);
    cstr_drop(&s);
}
```

## Display context
A `DisplayCtx` carries the output buffer and formatting options:

```text
struct DisplayCtx {
    cstr target;
    unsigned char indent;
    bool color : 1;
    bool multiline : 1;
    bool drop_after_display : 1;
    bool debug_mode : 1;
};
```

The default seed contexts are:
- `DEFAULT_DISPLAY`: used by `cstr_d`
- `PRINT_DISPLAY`: used by `print` and `println`; defaults to color and multiline
- `EPRINT_DISPLAY`: used by `eprint` and `eprintln`; defaults to `PRINT_DISPLAY`

Each is `#ifndef`-guarded, so a translation unit can define a different default before including `display.h`.

Pass a `DisplayCtx` as the first argument to override a single sequence:

```c
#include <cleat/display.h>
#include <cleat/string_helpers.h>

#undef DISPLAYABLES
#define DISPLAYABLES \
    CORE_DISPLAYABLES, \
    STRING_COLLECTION_DISPLAYABLES

void use(VecCStr const* strings) {
    cstr compact = cstr_d(*strings);
    cstr multiline = cstr_d(((DisplayCtx){.multiline = true}), *strings);

    cstr_drop(&compact);
    cstr_drop(&multiline);
}
```

`nl` inserts a newline plus the current indentation when `multiline` is set, and an empty string otherwise. `nl_or_space` uses a single space as the compact-mode fallback. The generated container displays use the same helpers to switch between compact and multiline output.

Low-level helpers for display implementations live in [display_functions.h](../include/cleat/display_functions.h):
- `lit_display("literal", d)` appends a string literal.
- `sv_display(sv, d)` appends a `csview` without quoting.
- `debuggable_sv_display(sv, d)` appends raw text or a debug string depending on `d.debug_mode`.
- `string_debug_display(sv, d)` appends a quoted, escaped string.
- `fmt_display(d, "...", ...)` appends `printf`-style formatted text.
- `color_display(code, d)` appends an ANSI color code only when `d.color` is set.

## Errors
`err_d(...)` from [failable.h](../include/cleat/failable.h) builds a `StringError` from the same kind of display sequence:

```c
#include <cleat/display.h>
#include <cleat/failable.h>

failable check_values(i32 actual, i32 expected) {
    if (actual != expected) return err_d("expected ", expected, ", got ", actual);
    return ok;
}
```
