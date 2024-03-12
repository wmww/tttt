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
    '-Wno-error=unused-variable',
    '-Wno-error=unused-parameter',
    '-Wno-error=unused-function',
    '-Winfinite-recursion',
    '-Werror=implicit-function-declaration',
    '-Werror=return-type',
    '-Werror=incompatible-pointer-types',
], language: 'c')

subdir('thirdparty')
subdir('src')

executable(
    'tttt',
    main_files,
    include_directories: thirdparty_inc,
    link_with: thirdparty_libs,
    link_args: '-lm',
)

executable(
    'baseline_tests',
    baseline_test_files,
    include_directories: thirdparty_inc,
    link_with: thirdparty_libs,
    link_args: '-lm',
)
