// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f<U extends object | string>() {
  const n: never = null! as (U & {} & { a: 1 }) | (U & string) | (U & object);
}
const sentinel: number = "";
