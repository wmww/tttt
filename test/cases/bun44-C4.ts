// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface I { a: 1 }
type A = I & { p: 1 };
function f<T extends A>(t: T) {
  const x: { q: 1 } = t;
}
const sentinel: number = "";
