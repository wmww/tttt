project('tttt',
    ['c'],
    version: '0.1.0',
    license: 'MIT',
    meson_version: '>=0.51.0',
    default_options: ['c_std=c2x', 'warning_level=3', 'werror=true'],
)

add_project_arguments([
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
], language: 'c')

if get_option('buildtype').startswith('debug')
  add_project_arguments('-DDEBUG_BUILD', language : 'c')
  add_project_arguments('-DINCLUDE_VALGRIND', language : 'c')
endif

subdir('thirdparty')
subdir('src')

executable(
    'tttt',
    main_files,
    include_directories: [thirdparty_inc, src_inc],
    link_with: thirdparty_libs,
    link_args: '-lm',
)

executable(
    'baseline_tests',
    baseline_test_files,
    include_directories: [thirdparty_inc, src_inc],
    link_with: thirdparty_libs,
    link_args: '-lm',
)

executable(
    'diffapp_runner',
    diffapp_runner_files,
    include_directories: [thirdparty_inc, src_inc],
    link_with: thirdparty_libs,
    link_args: '-lm',
    build_by_default: false,
)
