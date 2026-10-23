// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "target": "es2015", "lib": ["es2022"], "importHelpers": true, "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
export const a = f();
export async function g() {}
function f() { return C.get(new C()); }
class C { #x = 1; static get(c: C) { return c.#x; } }
export const sentinel: number = "";
