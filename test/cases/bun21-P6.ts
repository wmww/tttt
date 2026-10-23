// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f(v: [string]) { const [...r]: [string] | [number] = v; r[0].length; }
const sentinel: number = "";
