// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const a: unique symbol;
declare const sym: symbol;
const c4: unique symbol = a;
const c6: unique symbol = sym;
const c9: unique symbol = 1;
class C { static readonly s: unique symbol = a; }
const sentinel: number = "";
