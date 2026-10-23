// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function* g1(): Generator<number[]> { yield* [[1], ["a"]]; }
const sentinel: number = "";
