// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
try { } catch (x = 1) { let x = 2; }
const sentinel: number = "";
