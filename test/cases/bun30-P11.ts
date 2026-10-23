// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export class A { #a = 1; m(o: A) { let x; ({ #a: x } = o); return x; } }
const sentinel: number = "";
