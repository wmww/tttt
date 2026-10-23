// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] }, "files": ["a.ts"], "include": ["*.d.ts"] }
// @filename: a.d.ts
declare const g: number;
// @filename: a.ts
const x: number = g;
const sentinel: number = "";
