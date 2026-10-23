// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f1(x: string | number) { const y = <typeof x>(x = 1); const n: never = y; }
function f2(x: string | number) { x = 1; const y = <typeof x>(x = "a"); }
const sentinel: number = "";
