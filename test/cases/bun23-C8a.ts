// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const b: Required<{ ["foo"]: 1 }> = {};
const sentinel: number = "";
