#include "type.h"
#include "program.h"
#include "engine.h"
#include "error.h"
#include "display.h"
#include "source_file.h"
#include "string_helpers.h"
#include <cleat/filesystem.h>

static const char* const stdlib_names[] = {
    "default",
    "es2015.collection",
    "es2015.iterable",
    "es2015.generator",
};

static_failable load_stdlib(Program* program) {
    for (intptr_t i = 0; i < (intptr_t)c_arraylen(stdlib_names); i++) {
        try (SourceFile* lib, program_load_stdlib(program, csview_from(stdlib_names[i]), &lib)) bubble;
        ignore lib;
    }
    return ok;
}

static void destroy_program(Program* program, ErrorCtx* errs) {
    program_destroy(program);
    error_ctx_drop(errs);
}

// Prints and clears whatever errors have accumulated so far, returning how many there were
static intptr_t report_errors(ErrorCtx* errs) {
    if (error_ctx_empty(errs)) {
        return 0;
    }
    const intptr_t count = vec_error_size(&errs->errors);
    print_error_ctx(errs);
    error_ctx_clear(errs);
    return count;
}

// Checks the file at the given path, logically returning the exit code to leave with. Problems with the checked
// program are printed as TypeScript diagnostics and reflected in that exit code; only failures that stop us checking
// at all (an unreadable standard library or entry point) come back as errors.
static_failable check_file(csview path, returns int) {
    ErrorCtx errs = error_ctx_init();
    error_config_enable_strict(&errs.config);
    Program* program = program_new(&errs, fs);

    printf("%sLoading libraries...%s\n", color_good, color_normal);
    try (load_stdlib(program)) cleanup {
        destroy_program(program, &errs);
    }

    if (!error_ctx_empty(&errs)) {
        const intptr_t lib_errors = vec_error_size(&errs.errors);
        printf(
            "Encountered %s%zd errors%s loading libraries.\n",
            color_error, lib_errors, color_normal);
        //print_error_ctx(&errs);
        error_ctx_clear(&errs);
    }

    printf("%sLoading %.*s...%s\n", color_good, c_SV(path), color_normal);
    try (SourceFile* source, program_load(program, path, &source)) cleanup {
        destroy_program(program, &errs);
    }

    int ret = 0;
    const intptr_t parse_errors = report_errors(&errs);
    if (parse_errors) ret = 1;

    program_resolve_names(program);

    const intptr_t name_errors = report_errors(&errs);
    if (name_errors) ret = 1;

    cstr parsed_str = cstr_null;
    display_block(debug_display(&parsed_str), &source->ast);
    printf("%sParsed:%s\n", color_good, color_normal);
    printf("%s\n\n", cstr_str(&parsed_str));
    cstr_drop(&parsed_str);

    intptr_t type_errors = 0;
    if (ret == 0) {
        program_typecheck(program);

        if (error_ctx_empty(&errs)) {
            printf("%sTypecheck passed%s\n", color_good, color_normal);
        } else {
            printf("%sTypecheck failed%s\n", color_error, color_normal);
        }

        type_errors = report_errors(&errs);
        if (type_errors) ret = 1;
    }

    if (parse_errors || name_errors || type_errors) {
        printf(
            "Checked %zd files, found %s%zd parse errors%s, %s%zd name errors%s and %s%zd type errors%s.\n",
            program_get_file_count(program),
            parse_errors ? color_error : "",
            parse_errors,
            color_normal,
            name_errors ? color_error : "",
            name_errors,
            color_normal,
            type_errors ? color_error : "",
            type_errors,
            color_normal
        );
    } else {
        printf("%sChecked %zd files, no errors found!%s\n", color_good, program_get_file_count(program), color_normal);
    }

    //printf("%sRunning...%s\n", color_good, color_normal);
    //run_in_engine(source, arc_js_object_init());
    //printf("%sRun succeeded%s\n", color_good, color_normal);

    destroy_program(program, &errs);
    return ok(ret);
}

int main(int argc, const char** argv) {
    if (argc > 2) {
        printf("%sToo many arguments%s\n", color_error, color_normal);
        return 1;
    } else if (argc < 2) {
        printf("%sToo few arguments%s\n", color_error, color_normal);
        return 1;
    } else if (streq(argv[1], "--help") || streq(argv[1], "-h")) {
        printf(
            "The Tess TypeScript Typechecker\n\n"
            "USAGE: tttt [TSPATH]\n"
        );
        return 0;
    }

    try (int ret, check_file(csview_from(argv[1]), &ret)) if_err (e) {
        error_base_eprintln(e);
        current_error_destroy();
        return 1;
    }

    return ret;
}
