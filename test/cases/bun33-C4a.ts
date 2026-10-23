// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const d: any;
class A { @d(d as this) p = 1 }
const sentinel: number = "";
