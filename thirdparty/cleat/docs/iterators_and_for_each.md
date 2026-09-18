# Iterators and for-each
Cleat uses STC's iterators with some enhancements.

## for_each
The most common way to use them is with the `for_each` statement from [common_macros.h](../include/cleat/common_macros.h).
```c
#include <stdio.h>
#include <cleat/common_macros.h>
#include <cleat/string_helpers.h>

void iter_through_array(VecStr* arr) {
    // (item_name, container_prefix, container_value)
    for_each (i, vec_str, arr) {
        printf("%s\n", *i);
    }
}
```

`for_each` also gives you `NAME_iter` and `NAME_index` variables to access those values.
```c
#include <stdio.h>
#include <cleat/common_macros.h>
#include <cleat/string_helpers.h>

void iter_through_array(VecStr* arr) {
    for_each (i, vec_str, arr) {
        ignore i;
        printf("%ld. %s\n", i_index, *i_iter.ref);
        // i_iter.ref is just a more verbose way of accessing i, more useful if you want to do more complex things with
        // the iterator.
    }
}
```

## for_each_kv
`for_each_kv` can be used to iterate through maps with both keys and values
```c
#include <stdio.h>
#include <cleat/common_macros.h>

typedef int Int;
#define g_key Int, int
#define g_val Int, int
#include <cleat/generic/hmap.h>

void iter_through_array(HMapInt* map) {
    for_each_kv (key, val, hmap_int, map) {
        printf("%d: %d\n", *key, *val);
    }
}
```

## Iterators
Cleat uses STC's iterator convention with the additional `*_iter_deref()` function. The `_begin()` function can take multiple arguments.

### Implementing an iterator
```c
#include <cleat/common_macros.h>
#include <stdio.h>

typedef struct powers_of_iter powers_of_iter;
struct powers_of_iter {
    bool ref; // false to stop iteration
    i64 current;
    i64 power;
    i64 max;
};

powers_of_iter powers_of_begin(i64 power, i64 max) {
    return (powers_of_iter){
        .ref = true,
        .current = 1,
        .power = power,
        .max = max,
    };
}

void powers_of_next(powers_of_iter* me) {
    my current *= my power;
    my ref = my current < my max;
}

i64 powers_of_iter_deref(powers_of_iter* me) {
    return my current;
}

void use_powers_of() {
    for_each (x, powers_of, 3, 100) {
        printf("%ld\n", x);
    }
}
```

### *_iter_deref
This takes a pointer to an iterator and returns the value it's currently on. This is generally a pointer (for STC containers it's just the `ref` iterator member), but it can also be other values, such as a csview for `lines` iteration in [string_helpers.h](../include/cleat/string_helpers.h). `_iter_deref()` is needed becuse the `ref` iterator member must be castable to bool to detect if the iterator is done.
