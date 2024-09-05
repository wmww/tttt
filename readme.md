# The Tess TypeScript Typechecker

Work-in-progress [TypeScript](https://www.typescriptlang.org/) typechecker, alternative to [tsc](https://github.com/microsoft/TypeScript).

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
./fasttest.sh                   # Build with optimizations and run without Valgrind (flags same)
./diffapp.sh                    # Start the web app that lets you compare results with tsc
./diffapp.sh castingTuple       # Start with web app with a specific test
```

The test scripts will automatically clone the TypeScript repo at the right commit when needed. If that repo gets fucky then `rm -rf test/TypeScript` and re-run the scripts.
