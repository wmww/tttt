// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const c: "a";
function f(x: "a" | "b") { if (c !== "a") { if (x !== c) { const y: "b" = x; } } }
const sentinel: number = "";
