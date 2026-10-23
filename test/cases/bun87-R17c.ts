// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
var globalThis: number;
var x1 = 1;
const r1: 0 = globalThis;
const r2: 0 = globalThis.x1;
const r3: typeof globalThis = 1;
const sentinel: number = "";
