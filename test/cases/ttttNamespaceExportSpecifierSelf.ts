// @noEmit: true

// `export { v }` in a namespace or `declare global` resolves v in the module declaration's exports first, where it
// is the specifier itself (tsc's resolveName skips export specifier aliases only in source files and ambient
// non-global module declarations): TS2303 next to the misplaced-export error. A `declare namespace` doesn't cycle
export {};
const v = 1;
const w = 2;
namespace N { export { v }; }
namespace O { export { w as w2 }; }
declare global { export { v }; }
declare namespace P { export { v }; }
