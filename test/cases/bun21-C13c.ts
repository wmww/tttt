// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f(x: number | undefined, v: [number]) { [x!] = v; x.toFixed(); }
const sentinel: number = "";
