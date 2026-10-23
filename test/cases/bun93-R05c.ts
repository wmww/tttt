// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface A1 { a: 1 }
function f<U extends object | string>() {
  type T1 = U & {} & A1;
  type T2 = U & A1;
  const n: never = null! as T1 | (U & string) | (U & object);
  const n2: never = null! as T2 | (U & string) | (U & object);
}
const sentinel: number = "";
