// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const g1: { 3: number } = { get [+3]() { return 1; } };
const sentinel: number = "";
