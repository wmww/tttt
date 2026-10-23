// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f<T extends { a: string }>(x: Partial<T>, y: { b: number }) {
  x = y;
}
const sentinel: number = "";
