// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f(x: string | number, v: { a: { b: string } }) { ({ a: { b: x = 1 } } = v); x.toFixed(); }
const sentinel: number = "";
