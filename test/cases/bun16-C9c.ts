// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function fm() {} namespace fm { export type T = 1 }
if (fm) { let x: fm.T; }
const sentinel: number = "";
