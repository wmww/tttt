// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const u: unique symbol;
declare let a: (x: unknown) => x is typeof u;
declare let b: (x: unknown) => x is 1;
a = b;
const sentinel: number = "";
