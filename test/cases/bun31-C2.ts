// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function m(a?: { c: number }) { if (a?.c! === 1) a.c; }
const sentinel: number = "";
