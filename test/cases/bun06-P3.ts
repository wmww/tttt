// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class A { get r(): string { return "" } set r(v: string | number) { } }
declare const a: A;
a.r! = 1;
const sentinel: number = "";
