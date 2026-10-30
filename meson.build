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

# clang (unlike GCC) warns about designated initializers that leave fields out under -Wextra, which -Werror makes
# fatal in the vendored STC (csview.h's `{.buf = ...}` literals) and our own code alike: leaving fields to zero is C's
# rule and this code's style
cc = meson.get_compiler('c')
if cc.get_id() == 'clang'
    add_project_arguments('-Wno-missing-field-initializers', language : 'c')
    # C23 lets an enumerator go past int's range (several flag enums do); a clang before 20 (Apple clang 17) calls that
    # -Wpedantic, with no group of its own to turn off alone. And STC's c_foreach is __auto_type, a GNU extension to it
    if not cc.compiles('enum { BIG = 1ull << 40 }; int main(void) { return 0; }', args: ['-std=c2x', '-Wpedantic', '-Werror'], name: 'C23 wide enumerators')
        add_project_arguments('-Wno-pedantic', language : 'c')
    endif
    add_project_arguments('-Wno-gnu-auto-type', language : 'c')
endif

# macOS: the BSD and Darwin interfaces (DT_REG, MAP_ANON...) that a file's _XOPEN_SOURCE would otherwise hide. And
# ld64 refuses an undefined weak reference unless told: the LD_PRELOADed tools' hooks (main.c, program.c), which stay
# null there (DYLD_INSERT_LIBRARIES could fill them in)
link_args = ['-lm']
if host_machine.system() == 'darwin'
    add_project_arguments('-D_DARWIN_C_SOURCE', language : 'c')
    # (Apple clang still makes uninitialized globals common symbols, which ld64 aligns by their size: ast_segment_map's
    # MB asks 32 KB, past a segment's 16 KB, a warning that werror's -fatal_warnings makes fatal. GCC's and LLVM's default)
    add_project_arguments('-fno-common', language : 'c')
    foreach hook : ['memtrack_dump', 'memtrack_scan', 'timeline_phase']
        link_args += '-Wl,-U,_' + hook
    endforeach
endif

# Windows (MinGW-w64, UCRT): src/win32 stands in for the POSIX headers Windows lacks (sys/mman.h, sys/resource.h) and
# implements them (agents/notes/windows.md). GCC's struct layout, not MSVC's (-mno-ms-bitfields: the AST's bitfield
# structs keep their Linux sizes), a 64-bit off_t and a standalone executable (no MinGW runtime
# DLL: libgcc/compiler-rt and winpthreads linked in) whose threads each reserve 256 MB of stack, the most
# worker_stack_size() asks for, committed as it grows (Windows can't take a stack of ours: load_pool.c)
win32_files = []
if host_machine.system() == 'windows'
    add_project_arguments('-mno-ms-bitfields', '-D_FILE_OFFSET_BITS=64', '-I' + meson.current_source_dir() / 'src/win32', language : 'c')
    link_args += ['-static', '-Wl,--stack,268435456']
    win32_files += files('src/win32/win32.c')
    # (The process's code page is UTF-8: argv, getenv() and the C runtime's file functions take UTF-8 paths)
    win32_files += import('windows').compile_resources('src/win32/tttt.rc', depend_files: files('src/win32/tttt.manifest'))
    link_args += ['-lpsapi']
endif

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
    main_files + win32_files,
    include_directories: src_inc,
    dependencies: deps,
    c_args: cleat_c_args,
    link_args: link_args,
)

executable(
    'baseline_tests',
    baseline_test_files + win32_files,
    include_directories: src_inc,
    dependencies: deps,
    # (The source tree: where the suite's content mapper launcher is, thirdparty/typescript/built)
    c_args: cleat_c_args + ['-DDEBUG_ALLOCATOR', '-DTTTT_SOURCE_DIR="' + meson.current_source_dir() + '"'],
    link_args: link_args,
)

executable(
    'diffapp_runner',
    diffapp_runner_files + win32_files,
    include_directories: src_inc,
    dependencies: deps,
    c_args: cleat_c_args + ['-DDEBUG_ALLOCATOR'],
    link_args: link_args,
    build_by_default: false,
)
