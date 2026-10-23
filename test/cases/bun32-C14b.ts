// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f(x: string | number) { if (typeof x === "string") { () => x.length; enum E { A = (x = 1) } } }
const sentinel: number = "";
