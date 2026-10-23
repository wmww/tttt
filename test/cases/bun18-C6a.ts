// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f<T>(s: T & {}) {
  s as { a: 1 };
}
const sentinel: number = "";
