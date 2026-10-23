// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f<T extends any[]>(t: readonly [number, ...T]) {
  t["length"] = 1;
}
const sentinel: number = "";
