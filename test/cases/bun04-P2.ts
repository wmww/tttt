// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es5"] } }
// @filename: a.ts
declare const u1: string | { a: number } | { a: number; b: string };
for (const x of u1) {}
const sentinel: number = "";
