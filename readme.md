# The Tess TypeScript Typechecker

Under developent

```bash
./run.sh path/to/source.ts      # Build and run in Valgrind
./test.sh                       # Build and run against TypeScript tests in Valgrind
./test.sh -f castingTuple -d    # Run against specific test and show diff
./fasttest.sh                   # Build with optimizations and run without Valgrind
```

The test scripts will automatically clone the TypeScript repo at the right commit when needed. If that repo gets fucky `rm -rf test/TypeScript` and re-run the scripts.
