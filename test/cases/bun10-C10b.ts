// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const p8 = { set x(v: typeof p8.x) {} };
const sentinel: number = "";
