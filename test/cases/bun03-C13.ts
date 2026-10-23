// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class C { a = 1 }
interface C { new (): C; new (x: number): { a: number; b: number } }
declare const x: C;
class S extends x {}
const sentinel: number = "";
