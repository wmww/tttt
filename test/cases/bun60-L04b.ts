// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "declaration": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f() { const s = Symbol(); return { [s]: 1 }; }
export function g() {}
g["ab"] = f();
g.cd = f();
g[1] = f();
export const h = () => {};
h["ab"] = f();
h.cd = f();
const sentinel: number = "";
