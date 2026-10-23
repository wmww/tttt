// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function g<T extends U, U>(): T extends string ? (x: T) => U : 0 {
  const r: 0 | ((x: string) => unknown) = g<T, unknown>();
  return null!;
}
const sentinel: number = "";
