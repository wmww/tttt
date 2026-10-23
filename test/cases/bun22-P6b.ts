// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f<T>(cb: (x: T) => { ok: boolean }): T;
function g(x: { ok: boolean }) { return f(g); }
const sentinel: number = "";
