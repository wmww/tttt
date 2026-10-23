// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f<T>(cb: () => { ok: boolean }, y: T): T;
function g() { return f(g, { ok: true }); }
const sentinel: number = "";
