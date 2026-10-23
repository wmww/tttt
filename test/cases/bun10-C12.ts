// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function g1(this: ReturnType<typeof g1>) { return this; }
const sentinel: number = "";
