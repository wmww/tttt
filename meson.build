project('tttt',
    ['c'],
    version: '0.1.0',
    license: 'MIT',
    meson_version: '>=0.51.0',
    default_options: ['c_std=c2x', 'warning_level=3', 'werror=true'],
)

if get_option('buildtype').startswith('debug')
    add_project_arguments('-DDEBUG_BUILD', language : 'c')
    add_project_arguments('-DINCLUDE_VALGRIND', language : 'c')
endif

# Where main.c's set_lib_dir() finds the standard library when the binary isn't in a build directory of the repo
# (`meson setup /tmp/x`): the source tree's thirdparty/lib
add_project_arguments('-DTTTT_SOURCE_LIB_DIR="' + meson.current_source_dir() / 'thirdparty' / 'lib' + '"', language : 'c')

# Must come after the add_project_arguments() above so cleat gets the same defines. cleat's meson.build declares
# cleat_dep and cleat_c_args (the recommended set of warnings/errors); cleat_c_args can't go through
# add_project_arguments() because by this point cleat has already declared its own build targets, so each of our
# targets passes it as c_args instead.
subdir('thirdparty')
# (Not in thirdparty/meson.build, which refresh.sh writes)
deps = thirdparty_deps + [dependency('threads')]

subdir('src')

executable(
    'tttt',
    main_files,
    include_directories: src_inc,
    dependencies: deps,
    c_args: cleat_c_args,
    link_args: '-lm',
)

executable(
    'baseline_tests',
    baseline_test_files,
    include_directories: src_inc,
    dependencies: deps,
    c_args: cleat_c_args + ['-DDEBUG_ALLOCATOR'],
    link_args: '-lm',
)

executable(
    'diffapp_runner',
    diffapp_runner_files,
    include_directories: src_inc,
    dependencies: deps,
    c_args: cleat_c_args + ['-DDEBUG_ALLOCATOR'],
    link_args: '-lm',
    build_by_default: false,
)
