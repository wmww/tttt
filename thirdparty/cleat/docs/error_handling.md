# Error Handling
This is the documentation for how errors are propagated and handled in Cleat. [failable_minimal.h](../include/cleat/failable_minimal.h) contains the types and core macros needed to declare failable functions and custom errors (designed to be included from other headers) and [failable.h](../include/cleat/failable.h) includes the functions and macros for using them.

If you're just using an API with failable functions but don't want to deal with my bullshit, see `check_result()`, `error_base_eprintln()` and `error_base_destroy()` in `failable_types.h`. Using a failable function can be as simple as:
```c
#include <cleat/failable.h>
failable sometimes_fails();

int your_func() {
    FailableResult result = sometimes_fails();
    if (result.error) {
        error_base_eprintln(result.error);
        error_base_destroy(result.error);
        return 1;
    }
    // ...
    return 0;
}
```

If you want to use the failable system to detect different types of errors or ergonomically and safely propagate or handle them, read on.

## Concepts
### Failable functions
Functions that may produce errors are "failable". Failable functions result in `ok` or `err`. Like normal functions they logically return a specified type or return void, however in the `err` case no value is logically returned. "Logical" here refers to what the function should be understood as returning, in contrast to what is literally returned at the language level. Failable functions use output parameters to move their logical return values back to the calling function. More details on this below.

There are different types of errors, but failable functions do not specify what error types they produce. Keeping error types dynamic (like C++ and TypeScript, unlike Rust) trades a little compile-time assurance and performance for significantly better ergonomics and simplicity.

### Error propagation and handling
Functions that call failable functions choose how to handle errors at each call site. If they themselves are failable they can "bubble" them up (like the rust `?` operator or the implicit behavior of exceptions in many languages). Functions can also explicitly handle errors, panic or ignore them. These options do not require the calling function to be failable.

### Error types
Error types exist within a single-inheritance class hierarchy. The most common types are `ErrorBase` (the abstract base class of all errors) and `StringError` (the default concrete implementation). Other error types may be useful for handling specific errors, errors from specific subsystems or attaching data to errors beyond just a message string.

## API by example
The simplest failable function looks like this:
```c
#include <cleat/failable.h>

failable always_succeed() {
    return ok;
}
```

One that fails:
```c
#include <cleat/failable.h>

failable sometimes_fails(int a) {
    if (a > 0) {
        return ok;
    } else {
        return err("a is negative");
    }
}
```

### Calling failable functions
Failable functions can be called with a `try () ...` statement (implemented with macros in `failable.h`). It makes it easy to use and compose different error handling strategies, and handles error memory management in most cases.
```c
#include <cleat/failable.h>
#include <cleat/log.h>
failable sometimes_fails(int a);

void caller() {
    // Abort the program with a non-zero exit code
    try (sometimes_fails(0)) or_panic;

    // Log an error to stderr but otherwise ignore it
    try (sometimes_fails(0)) or_log(warn);

    // Do nothing
    try (sometimes_fails(0)) or_ignore;

    // Handle the error, it is automatically destroyed after the if_err block
    try (sometimes_fails(0)) if_err (e) {
        if (error_downcast(e, &StringErrorVt)) {
            log_info("string error: ", e);
        } else {
            log_info("non-string error: ", e);
        }
    }
}
```

If the caller is itself failable, `bubble` and `cleanup {}` are possible:
```c
#include <cleat/failable.h>
failable sometimes_fails(int a);

failable caller() {
    // Immediately "bubble up" any error produced (very common for exception-like behavior)
    try (sometimes_fails(0)) bubble;

    // Similar to `bubble`, but allows you to perform final cleanup before the function exits
    try (sometimes_fails(0)) cleanup {
        // Free resources here
    }

    return ok;
}
```

### Returning values
All failable functions return the internal `FailableResult` type at the language level, however the functions we've seen so far "logically" return void. To write a function that logically returns a value, put its return type after `returns` in the argument list like so:
```c
#include <cleat/failable.h>

failable add_one(int a, returns int) {
    if (a > 0) {
        return ok(a + 1);
    } else {
        return err("a is negative");
    }
}
```
Note that our return value goes in parenthesis after `ok` in this case. Because this function returns int, `return ok;` would produce a compile-time error here just as `return ok(...) would produce a compile-time error in a function logically returning void. How? You don't want to know.

To call a value-returning failable function you need to pass a pointer to a value of the correct type to be "filled". A form of the try statement that includes a declaration makes this easy.
```c
#include <cleat/failable.h>
failable add_one(int a, returns int);

failable caller() {
    // You could do this
    int x = 0;
    try (add_one(3, &x)) bubble;

    // But this is cleaner
    try (int y, add_one(3, &y)) bubble;

    return ok;
}
```
The declaration form of the try statement also has the benefit of allowing you to control how all failable return variables are initialized at once. By defining `FAILABLE_TRY_INITIALIZER` as ` ` (instead of the default `= {0}`) you can disable zero-initialization for performance (don't do this) or because uninitialized data can be caught more easily than incorrectly zeroed data by tools like Valgrind.

Do be careful of uninitialized data. I've tried to make it as hard as possible to slip up here, but it's still a minor footgun. A return value is undefined unless the function returns `ok`. It's Not zero. Not what it was before. Undefined. (it's generally initialized to zero and left unchanged by an `err`-returning function, but that is a bad idea to rely on)
```c
#include <cleat/failable.h>
failable add_one(int a, returns int);

int caller_1() {
    try (int x, add_one(3, &x)) or_ignore;
    return x; // NO!
}

int caller_2() {
    try (int x, add_one(3, &x)) if_err (e) {
        x = 0;
    }
    return x; // this is fine
}
```

### Advanced usage
`unconditional {}` and `if_ok {}` blocks are also possible in a `try` statement.
```c
#include <cleat/failable.h>
#include <cleat/log.h>
failable sometimes_fails(int a);

failable caller() {
    // if_ok {} comes before the final clause, and will run only if no error is produced
    try (sometimes_fails(0)) if_ok {
        printf("All good!\n");
    } or_log(error);

    try (sometimes_fails(0)) unconditional {
        // Useful for cleaning up resources used by the above call and now no longer needed
    } cleanup {
        // Clean up resources here that you would otherwise need later in the function
    };
}
```

You can handle specific error types. If the returned error is the given type or a subtype it will get caught.
```c
#include <cleat/failable.h>
failable sometimes_fails(int a);

typedef struct CustomError CustomError;
struct CustomError { ErrorBase error; };
extern ErrorVt const CustomErrorVt;

failable caller() {
    // Abort the program with a non-zero exit code
    try (sometimes_fails(0)) if_err (StringError, s) {
        // Handle simple errors
    } if_err (CustomError, c) {
        // Handle custom errors
    } bubble; // Bubble up any other type of errors
    return ok;
}
```

An arbitrary number of errors can be bundled together using `or_collect_into (result)`.
```c
#include <cleat.h>
failable sometimes_fails(csview x, returns int);

failable caller(VecCStr* vec) {
    FailableResult result = {0};
    for_each (i, vec_cstr, vec) {
        try (int x, sometimes_fails(cstr_sv(i), &x)) if_ok {
            printf("%d\n", x);
        } or_collect_into (&result);
    }
    try (result) bubble;
    return ok;
}
```

A `try (...) ...` statement may have an `unconditional {}` block, an `if_ok {}` block and any number of `if_err (Type, name) {}`, but must always be completed by one of `if_err (name)` (catch-all), `bubble`, `cleanup {}`, `or_collect_into (result)`, `or_panic`, `or_log(type)`, `or_ignore`. These constraints are generally enforced by the compiler in most cases.

You can return from inside an `if_ok`, `if_err`, or `cleanup` block, however when returning from an `if_err` or `cleanup` block care must be taken to correctly manage the lifetime of the current error. The `current_error_move()` and `current_error_destroy()` helpers are available for this.
```c
#include <cleat.h>
failable sometimes_fails(int a);

failable caller() {
    ErrorBase* error = nullptr;
    try (sometimes_fails(0)) if_err (e) {
        switch (rand() % 3) {
            case 0:
                // current_error_destroy() makes it safe to jump out of the if_err block (via return or goto), without
                // leaking the caught error
                current_error_destroy();
                return err("blah");

            case 1:
                // current_error_move() prevents the current error from being automatically destroyed at the end of the
                // if_err block. It also returns the current ErrorBase* (note that unlike e, this pointer is non-const,
                // indicating you now have ownership of it)
                error = current_error_move();
                break;

            case 2:
                // You can also return the current error.
                return err(current_error_move());
        }
    }

    if (error) {
        // The err() macro can be used with a `ErrorBase*` as well as a string, it just works!
        return err(error);
    }

    return ok;
}
```

To use functions with alternative error handling (such as standard C functions) with the failable system, you can use the `ok_if(...)` helper macro. If the expression you give it evaluates truthy it evaluates to `ok`, and otherwise it evaluates to `err`. In the `err` case its error message is the stringified version of the code you gave it (this is a macro feature and has no runtime cost). For example:
```c
#include <cleat/failable.h>

failable get_number_from_file(const char* path, returns int) {
    // Explicit method, gives you control over the error message
    FILE* f0 = fopen(path, "rb");
    if (!f0) return err("failed to open file");
    fclose(f0);

    // More compact method with ok_if()
    FILE* f1 = fopen(path, "rb");
    try (ok_if(f1)) bubble;
    fclose(f1);

    // Even more compact, but arguably less readable. takes advantage of how
    // assignment is an expression. Note that in this case the error message
    // will be something like "f = fopen(path, \"rb\") failed" whereas in the
    // above case it would just be "f failed"
    try (FILE* f2, ok_if(f2 = fopen(path, "rb"))) bubble;

    // ok_if composes with all the other try () features
    try (
        int number,
        ok_if(fread(&number, sizeof(number), 1, f2) == 1)
    ) cleanup {
        fclose(f2);
    }
    fclose(f2);
    return ok(number);
}
```

To add qualifiers to failable functions there is `static_failable` and `static_inline_failable`:
```c
#include <cleat/failable.h>

static_failable foo() { return ok; }
static_inline_failable bar() { return ok; }
// Or just
[[nodiscard]] FailableResult baz() { return ok; }
```

Finally the `err_d()` macro can be used to create an error from a sequence of [displayable](display.md) items, or `err_fmt("...", ...)` can be used for more traditional formatting:
```c
#include <cleat.h>

static_failable always_fails() {
    if (rand() % 2)
        return err_d(1, " + ", 1, " = ", 2);
    else
        return err_fmt("%d + %d = %d", 1, 1, 2);
}
```

Hopefully this is a good reference for interacting with and writing failable functions. For a deeper understanding feel free to read [failable.h](../include/cleat/failable.h). I've tried to comment it best I can, but some of the macros are inherently gnarly.
