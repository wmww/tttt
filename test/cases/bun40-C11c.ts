// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] }, "files": ["a.ts", "b.TS"] }
// @filename: a.ts
b;
const sentinel: number = "";
// @filename: b.ts
declare const b: 1;
