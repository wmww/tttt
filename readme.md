# The Tess TypeScript Typechecker

```
meson setup build
ninja -C build && valgrind --leak-check=full --show-leak-kinds=all ./build/tttt ./test/toy.ts
```
