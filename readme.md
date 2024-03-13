# The Tess TypeScript Typechecker

```
meson setup build
ninja -C build && valgrind --leak-check=full --show-leak-kinds=all --quiet ./build/tttt ./test/toy.ts
ninja -C build && valgrind --leak-check=full --show-leak-kinds=all --exit-on-first-error=yes --error-exitcode=1 --quiet ./build/baseline_tests
```
