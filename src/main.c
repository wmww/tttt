#include "type.h"
#include "program.h"
#include "engine.h"
#include "error.h"
#include "display.h"
#include "source_file.h"
#include "string_helpers.h"
#include "filesystem.h"

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

    ErrorCtx errs = error_ctx_init();
    error_config_enable_strict(&errs.config);
    Program* program = program_new(&errs, &real_fs_quiet);
    int ret = 0;
    intptr_t parse_errors = 0;
    intptr_t name_errors = 0;
    intptr_t type_errors = 0;

    printf("%sLoading libraries...%s\n", color_good, color_normal);
    program_load_stdlib(program, c_sv("default"));
    program_load_stdlib(program, c_sv("es2015.collection"));
    program_load_stdlib(program, c_sv("es2015.iterable"));
    program_load_stdlib(program, c_sv("es2015.generator"));

    if (!error_ctx_empty(&errs)) {
        const intptr_t lib_errors = vec_error_size(&errs.errors);
        printf(
            "Encountered %s%zd errors%s loading libraries.\n",
            color_error, lib_errors, color_normal);
        //print_error_ctx(&errs);
        error_ctx_clear(&errs);
    }

    printf("%sLoading %s...%s\n", color_good, argv[1], color_normal);
    SourceFile const* source = program_load(program, csview_from(argv[1]));

    if (!error_ctx_empty(&errs)) {
        parse_errors = vec_error_size(&errs.errors);
        print_error_ctx(&errs);
        error_ctx_clear(&errs);
        ret = 1;
    }

    program_resolve_names(program);

    if (!error_ctx_empty(&errs)) {
        name_errors = vec_error_size(&errs.errors);
        print_error_ctx(&errs);
        error_ctx_clear(&errs);
        ret = 1;
    }

    cstr parsed_str = cstr_null;
    display_block(debug_display(&parsed_str), &source->ast);
    printf("%sParsed:%s\n", color_good, color_normal);
    printf("%s\n\n", cstr_str(&parsed_str));
    cstr_drop(&parsed_str);

    if (ret == 0) {
        program_typecheck(program);

        if (error_ctx_empty(&errs)) {
            printf("%sTypecheck passed%s\n", color_good, color_normal);
        } else {
            printf("%sTypecheck failed%s\n", color_error, color_normal);
        }

        if (!error_ctx_empty(&errs)) {
            type_errors = vec_error_size(&errs.errors);
            print_error_ctx(&errs);
            error_ctx_clear(&errs);
            ret = 1;
        }
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

    program_destroy(program);
    error_ctx_drop(&errs);
    return ret;
}
