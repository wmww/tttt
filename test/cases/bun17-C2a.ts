// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Opt<T> = { [K in keyof T]?: number };
function g<T extends { a: string }>(x: Opt<T>, y: { c: number }) {
  x = y;
}
const sentinel: number = "";
