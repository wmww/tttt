// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const s1: { (c: typeof Symbol.iterator): void } = {};
const sentinel: number = "";
