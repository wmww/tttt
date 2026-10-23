// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f(x: "a" | "b", s: string) { [x] = s + s; }
const sentinel: number = "";
