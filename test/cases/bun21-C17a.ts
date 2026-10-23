// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f(x: { a?: { b: string | number } }, y: typeof x) { if (typeof x.a!.b === "string") { x = y; x.a!.b.length; } }
const sentinel: number = "";
