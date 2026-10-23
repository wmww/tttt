// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f<T extends unknown[]>(x: T) {
  const t: [number, ...T] = ["s", ...x];
}
const sentinel: number = "";
