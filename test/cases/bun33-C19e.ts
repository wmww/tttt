// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class P { #x = 1; m() { #x as const; } }
const sentinel: number = "";
