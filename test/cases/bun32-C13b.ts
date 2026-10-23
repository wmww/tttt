// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f(a: { b?: { c: string | number } }) {
  if (typeof a.b!.c === "string") { a = { b: { c: 1 } }; a.b!.c.length; }
}
const sentinel: number = "";
