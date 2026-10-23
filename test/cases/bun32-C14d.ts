// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f(x: string | number) { if (typeof x === "string") { () => x.length; let y: { [(x = 1)]: number }; } }
const sentinel: number = "";
