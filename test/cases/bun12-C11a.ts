// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const r2: unique symbol[];
let s2: string = r2;
const sentinel: number = "";
