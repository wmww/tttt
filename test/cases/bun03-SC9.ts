// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const x: any;
with (x) { interface K { a: number; a: string } }
const sentinel: number = "";
