// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f(x: string | { length: 1 }) { if (typeof x === "string" && x.length === undefined) x.toUpperCase(); }
const sentinel: number = "";
