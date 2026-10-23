// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const o = Object.freeze({ *[Symbol.iterator]() { yield 1; } });
const sentinel: number = "";
