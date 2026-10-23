// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface I { m: { [K in keyof this]: 1 } }
function f<T extends I>(t: T) { return t.m; }
declare let u: (I & { a: 1 }) | (I & { b: 1 });
const ok: { a: 1, m: 1 } | { b: 1, m: 1 } = f(u);
const sentinel: number = "";
