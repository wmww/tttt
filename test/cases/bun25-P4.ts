// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const o: { p(v: unknown): asserts v is string } | undefined;
function f(v: unknown) { o?.p(v); }
const sentinel: number = "";
