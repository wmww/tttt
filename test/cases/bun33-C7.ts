// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const h: { a(x: unknown): asserts x } | undefined;
h?.a(1);
const sentinel: number = "";
