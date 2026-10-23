// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const o: { 0: number; z: number };
const { [/*c*/ 0]: a0, ...rest } = o;
const r: { z: number; x: 1 } = rest;
const sentinel: number = "";
