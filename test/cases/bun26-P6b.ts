// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f<T extends string | undefined>(x: T & string) {
  const t: T & { a: 1 } = x;
}
const sentinel: number = "";
