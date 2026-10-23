// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
enum E { G = 10 ** -23 }
const g: E.G = 1e-23;
const sentinel: number = "";
