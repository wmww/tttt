// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface A1 { a: 1 }
function f<U extends object | string>() {
  type T = U & {} & A1;
  const n: never = null! as T | (U & string) | (U & object);
  const n2: never = null! as (U & A1) | (U & string) | (U & object);
}
const sentinel: number = "";
