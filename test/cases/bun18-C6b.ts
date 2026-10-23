// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f<T>(u: T & string) {
  u as { a: 1 } & { b: 2 };
}
const sentinel: number = "";
