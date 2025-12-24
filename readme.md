# The Tess TypeScript Typechecker

Incomplete [TypeScript](https://www.typescriptlang.org/) typechecker.

**Development on this project is halted indefinitely**, [retrospective](https://loginasroot.net/i_tried_to_write_a_typescript_type_checker)

Internal documentation: [documentation.md](documentation.md)

To compile and run:
```back
meson setup build --buildtype release
ninja -C build
build/tttt path/to/source.ts
```

Scripts useful for development:
```bash
./run.sh path/to/source.ts      # Build and run in Valgrind
./test.sh                       # Build and run against TypeScript tests in Valgrind
./test.sh -f castingTuple -d    # Run against specific test and show diff
./test.sh -f castingTuple -sym --typ  # Show only symbol and type diffs
./test.sh -f 'castingTuple|.*array.*' # Filters have full regex support
./fasttest.sh                   # Build with optimizations and run without Valgrind (same flags as test.sh)
./diffapp.sh                    # Start the web app that lets you compare results with tsc
./diffapp.sh castingTuple       # Start with web app with a specific test
./filetest.sh foo/bar.ts -d     # Compare test output of tttt and tsc on arbitrary file (first argument must be file path, other flags same as test.sh)
```

