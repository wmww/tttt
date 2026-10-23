// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler", "noUnusedLocals": true, "noUnusedParameters": true, "allowJs": true, "checkJs": true } }
// @filename: before-stmt.ts
import data from "./d.json" with { type: "json" };
type N = import("./a", { with: { k: 0 as import("./c", { with: { inner: 1 } }), outer: 2 } });
export type { N };
export { data };
// @filename: doc.js
/** @type {import("./j", { with: { a: (p) => p, b: 0 } })} */
export let a, b;
/**
 * @param {import("./k", { with: { "resolution-mode": "import" } })} x
 * @param {import("./k", { with: { k: import("./kk", { with: { z: "1" } }) } })} y
 */
export function g(x, y) { return [x, y]; }
// @filename: export-nested.ts
export * from "./d" with { type: 0 as import("./c", { with: { inner: 1 } }), outer: 2 };
export { z } from "./e" with { a: "b" };
// @filename: in-function.ts
type T = import("./m", { with: { a: () => { let u: import("./n", { with: { b: { x: 1 }, c: () => 2 } }); return u; } } });
export type { T };
// @filename: mode.ts
type A = import("./a", { with: { "resolution-mode": 0 as import("./c", { with: { "resolution-mode": "require" } }) } });
type C<X> = import("./g", { with: { a: 0 as X, b: (unused: number) => { const local = 1; } } });
function f(p: number) { let u: import("./n", { with: { c: () => p, d: undeclared } }); return u; }
export { f };
export type { A, C };
// @filename: nested-outer-first.ts
type N = import("./a", { with: { outer: 2, k: 0 as import("./c", { with: { inner: 1 } }) } });
export type { N };
// @filename: nested.ts
type N = import("./a", { with: { k: 0 as import("./c", { with: { inner: 1 } }), outer: 2 } });
export type { N };
// @filename: quoted.ts
type N = import("./a", { with: { "k": 0 as import("./c", { with: { "inner": "v1" } }), "outer": "v2" } });
export type { N };
// @filename: scope.ts
type T = import("./m", { with: { a: (p: number) => { let u: import("./n", { with: { c: () => p } }); return u; } } });
type G<X> = import("./g", { with: { a: 0 as X } });
export type { T, G };
// @filename: stmt-nested.ts
import data from "./d.json" with { type: 0 as import("./c", { with: { inner: 1 } }), outer: 2 };
export { data };
// @filename: three-deep.ts
const q = 1;
type T = import("./m", { with: { a: 0 as import("./n", { with: { x: 0 as import("./o", { with: { y: () => q } }), x2: () => q } }), c: () => q } });
export type { T };
// @filename: two-inner.ts
const q = 1;
type T = import("./m", { with: { a: 0 as import("./n", { with: { x: () => q } }), b: 0 as import("./o", { with: { y: () => q } }), c: () => q } });
export type { T };
