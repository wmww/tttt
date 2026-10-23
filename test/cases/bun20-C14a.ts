// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class A { a = 1 } class C { c = 3 } declare const AorC: typeof A | typeof C;
function f<TU extends unknown>(x: TU) { if (x instanceof AorC) { const a: null = x; } }
const sentinel: number = "";
