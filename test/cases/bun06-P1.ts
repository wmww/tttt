// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function* y1(): Generator<never> { yield* 1; }
const sentinel: number = "";
