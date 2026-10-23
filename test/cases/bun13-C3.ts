// @filename: tsconfig.json
{ "compilerOptions": { "strict": false, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function isObj(x: unknown): x is {};
function f1(x: unknown) { if (!isObj(x)) { const y: string = x; } }
const sentinel: number = "";
