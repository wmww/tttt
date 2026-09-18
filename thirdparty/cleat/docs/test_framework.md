# Test Framework
[test.h](../include/cleat/test.h) contains Cleat's test framework. Tests are plain C functions registered by macros at startup, grouped into modules, and run by a default `main` that ships with the framework. The simplest test looks like this:
```c
#include <cleat/test.h>

test(arithmetic_works) {
    test_int_eq(2 + 2, 4);
}
```

**This document was written by Claude Opus 4.7 and human-reviewed, it may be of lower quality than the human-written documentation**

## Defining tests
The `test()` macro defines and registers a test in one go. The argument is a C identifier; the body follows in a code block.
```c
#include <cleat/test.h>

test(strings_compare_equal) {
    test_str_eq("foo", "foo");
}

test(addition_is_commutative) {
    test_int_eq(2 + 3, 3 + 2);
}
```
A test belongs to a module. By default the module is derived from the file name: `test/foo_bar.c` becomes module `foo_bar` (a leading `test/` or `tests/` and the `.c` suffix are stripped). Tests are reported and filtered by `module::name`.

## Assertions
Assertions don't abort the test; failures are collected and reported at the end. A test passes if it produced no failures.
```c
#include <cleat/test.h>

test(everything) {
    // Booleans
    test_true(1 + 1 == 2);
    test_false(1 + 1 == 3);

    // Integers (any integer-compatible expression)
    test_int_eq(2 + 2, 4);

    // Pointers
    int x = 0;
    int* p = &x;
    test_nonnull(p);
    test_null((int*)nullptr);
    test_ptr_eq(p, &x);

    // Strings
    test_str_eq("foo", "foo");                     // const char*
    test_sv_eq(c_sv("foo"), "foo");                // csview
    test_cstr_eq(cstr_lit("foo"), "foo");          // cstr (the cstr is consumed)
    test_regex_matches(c_sv("foobar"), "fo+ba+r"); // csview against a regex
}
```
For ad-hoc failures with no built-in matcher use `test_fail_message("format string", args...)`.

## Working with failable functions
The framework hooks into [error handling](error_handling.md): the `try (...)` statement gains `test_ok` and `test_err` clauses, and existing clauses like `if_ok` and `bubble` still work as expected.
```c
#include <cleat/test.h>
#include <cleat/failable.h>

failable sometimes_fails(int a) {
    if (a > 0) return ok;
    return err("a is not positive");
}

test(failable_function) {
    // Pass if ok
    try (sometimes_fails(1)) test_ok;

    // Pass if the error has the given type and message
    try (sometimes_fails(0)) test_err (StringError, "a is not positive");
    try (sometimes_fails(0)) test_err (ErrorBase,  "a is not positive");
}
```
`test_ok_or_bail` is like `test_ok` but `return`s from the test on err. Useful when later code in the test depends on the call having succeeded:
```c
#include <cleat/test.h>
#include <cleat/failable.h>
failable get_value(returns int);

test(uses_setup) {
    try (int x, get_value(&x)) test_ok_or_bail;
    test_int_eq(x, 42); // only reached if get_value() returned ok
}
```

## Capturing output
`test_capture { ... }` runs its block in a fork and captures stdout, stderr, and the exit code. Inspect them through `captured` or compare all three at once with `test_captured_eq()`.
```c
#include <cleat/test.h>
#include <stdio.h>
#include <stdlib.h>

test(captures_stdout_stderr_exit) {
    test_capture {
        printf("hello\n");
        fprintf(stderr, "oops\n");
        exit(3);
    }
    test_captured_eq("hello\n", "oops\n", 3);

    test_capture {
        printf("just stdout\n");
    }
    test_str_eq(captured.out, "just stdout\n");
    test_str_eq(captured.err, "");
    test_int_eq(captured.exit, 0);
}
```
Because the block runs in a fork, mutations to test-local state inside `test_capture` don't survive after the block. The block exits the fork at its closing brace, so don't put cleanup that the parent process needs at the end of it.

## Dynamic tests
When you don't know your tests at compile time (they come from data files, generated cases, etc), `register_dynamic_tests` runs a block at startup where you can register as many tests as you like.
```c
#include <cleat/test.h>

static void check_value(TestCtx* test_ctx, void const* test_data) {
    int const* expected = test_data;
    test_int_eq(*expected, 42);
}

static int values[] = {42, 42, 100};

register_dynamic_tests {
    for (int i = 0; i < 3; i++) {
        cstr name = cstr_from_fmt("case %d", i);
        register_test(c_sv("dynamic"), cstr_sv(&name), check_value, nullptr, &values[i]);
        cstr_drop(&name);
    }
}
```
`register_test()` arguments:
- `module`, `name`: identify the test. `module` may be any string without `::` or newlines.
- `impl`: the test function, signature `void (TestCtx*, void const*)`.
- `drop`: optional destructor for `test_data`, called after the test runs. Pass `nullptr` if `test_data` doesn't need cleanup.
- `test_data`: opaque pointer passed to `impl` and `drop`. Must remain valid after the call.

If something goes wrong while registering tests (e.g. a data file fails to parse), report it with `register_test_problem()`. It registers a test that always fails with the given message, so the issue surfaces in the normal output rather than being silently dropped.
```c
#include <cleat/test.h>
#include <cleat/filesystem.h>
#include <cleat/failable.h>

register_dynamic_tests {
    try (cstr data, fs_read_file(fs, c_sv("cases.txt"), &data)) if_err (e) {
        register_test_problem(c_sv("cases"), c_sv("loading cases.txt"), error_base_get_message(e));
        current_error_destroy();
        return;
    }
    // ... register one test per case ...
    cstr_drop(&data);
}
```

Inside a test, the `test_ctx` parameter is opaque and only used by the assertion macros, which expect it to be in scope (`test()` and `register_dynamic_tests` arrange this for you).

## Building
The framework ships as the `cleat_test` Meson dependency, which pulls in `cleat_dep` along with `test.c`, the default test runner, the default stdout output, and the markdown code-block helper. The default runner provides `main()`, so test source files don't define one.
```meson
subdir('path/to/cleat')

executable('tests',
    files('test_one.c', 'test_two.c'),
    c_args: cleat_c_args,
    dependencies: [cleat_test_dep],
)
```

Run the resulting binary directly. With no arguments it runs everything; positional arguments are regex filters that match against `module`, `name`, or `module::name`. Multiple filters are combined with OR.
```bash
./build/tests                  # run everything
./build/tests failable         # run tests in module 'failable' (or with 'failable' in name)
./build/tests 'failable::ok.*' # run tests matching the regex against module::name
./build/tests foo bar          # run tests matching 'foo' or 'bar'
./build/tests --help           # show runner help
```
Filter regexes are case-insensitive full matches.

## Testing markdown code blocks
[test_docs_code.h](../include/cleat/test_docs_code.h) walks markdown files, extracts every fenced code block, and registers one test per block. You supply the per-block check; what "passing" means is up to you.
```c
#include <cleat/test.h>
#include <cleat/test_docs_code.h>

static void check_block(TestCtx* test_ctx, TestDocsCodeBlockData const* data) {
    // data->code_path:  path to a temp file containing just this block
    // data->docs_path:  the markdown file the block came from
    // data->docs_line:  line number of the opening ``` in the markdown
    // data->language:   the tag after ``` (e.g. "c", "bash", or "")
    if (cstr_equals(&data->language, "c")) {
        // compile data->code_path, run it, check output, etc.
    } else if (!cstr_equals(&data->language, "bash")) {
        test_fail_message("unsupported language '%s'", cstr_str(&data->language));
    }
    (void)test_ctx;
}

register_dynamic_tests {
    test_docs_code_register_directory(c_sv("docs"), check_block);
}
```
`test_docs_code_register_directory()` registers blocks from every `.md` file in a directory; `test_docs_code_register_markdown_file()` does it for a single file. Each markdown file becomes its own test module, and each block becomes a test named `code block on line N`. Markdown links to local files are also checked: a broken link registers a failing `links valid` test for that file.

Cleat's own test for this lives in [test/test_docs.c](../test/test_docs.c) and runs `gcc -fsyntax-only` over every C block in the project's markdown — which is also what catches mistakes in this very file.

## Custom output and runners
For most users the default output (stdout, with terminal colors) and default runner (CLI parsing plus a call to `run_tests()`) are fine. If you need something else — an alternate output format, an embedded runner, etc. — see [test_runner.h](../include/cleat/test_runner.h). Implement a `TestOutput` struct of callbacks and pass it to `run_tests()`:
```c
#include <cleat/test_runner.h>
#include <stdio.h>

static void on_test_finished(void* data, TestData const* test, bool is_pass, VecTestFailure failures) {
    (void)data;
    printf("%s::%s: %s\n", cstr_str(&test->module), cstr_str(&test->name), is_pass ? "PASS" : "FAIL");
    vec_test_failure_drop(&failures); // the callback owns failures
}

int main(void) {
    TestOutput output = { .test_finished = on_test_finished };
    bool passed = run_tests((RunTestsArgs){ .output = &output });
    drop_test_modules();
    return passed ? 0 : 1;
}
```
To replace the runner entirely don't link `default_test_runner.c`; [default_test_runner.c](../src/default_test_runner.c) and [stdout_test_output.c](../src/stdout_test_output.c) are short and the easiest reference.
