// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class C { static { let g = 1; var g = 2; } }
const sentinel: number = "";
