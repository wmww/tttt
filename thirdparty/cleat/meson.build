cleat_c_args = [
    '-fno-strict-aliasing',
    '-Wno-missing-braces',
    '-Wno-unused-function',
    '-Wno-format-zero-length',
    '-Wno-error=unused-variable',
    '-Wno-error=unused-parameter',
    '-Wno-error=unused-function',
    '-Wno-error=unused-but-set-variable',
    '-Wno-error=format-zero-length',
    '-Winfinite-recursion',
    '-Werror=implicit-function-declaration',
    '-Werror=return-type',
    '-Werror=incompatible-pointer-types',
]

stc_dep = declare_dependency(
    link_with: static_library(
        'stc',
        files(
            'stc/src/cregex.c',
            'stc/src/cspan.c',
            'stc/src/cstr_core.c',
            'stc/src/cstr_misc.c',
            'stc/src/cstr_io.c',
            'stc/src/cstr_utf8.c',
            'stc/src/utf8_misc.c',
            'stc/src/csview.c',
            'stc/src/fmt.c',
            'stc/src/random.c',
            'stc/src/stc_core.c',
        ),
        c_args: cleat_c_args,
    ),
    include_directories: include_directories('stc/include'),
)

cleat_dep = declare_dependency(
    link_with: static_library(
        'cleat',
        files(
            'src/string_helpers.c',
            'src/time_helpers.c',
            'src/log.c',
            'src/filesystem.c',
            'src/real_filesystem.c',
            'src/subproc.c',
            'src/display.c',
            'src/failable.c',
            'src/uri.c',
            'src/base64.c',
            'src/random.c',
            'src/seri.c',
            'src/seri_json.c',
            'src/seri_number_parsing.c',
        ),
        c_args: cleat_c_args,
        dependencies: [stc_dep],
        link_args: '-lm',
        include_directories: include_directories('include', 'include/cleat'),
    ),
    dependencies: [stc_dep],
    include_directories: include_directories('include'),
)

cleat_test_dep = declare_dependency(
    link_with: static_library(
        'cleat_test',
        files(
            'src/test.c',
            'src/default_test_runner.c',
            'src/stdout_test_output.c',
            'src/test_docs_code.c',
        ),
        c_args: cleat_c_args,
        dependencies: [cleat_dep],
        link_args: '-lm',
        include_directories: include_directories('include/cleat'),
    ),
    dependencies: [cleat_dep],
)
