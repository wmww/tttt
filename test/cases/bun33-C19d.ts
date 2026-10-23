// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class P { #x = 1; m() { const r: bigint = { [#x]: 1 }; } }
const sentinel: number = "";
