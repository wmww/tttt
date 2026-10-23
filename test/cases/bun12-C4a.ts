// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f<T extends { "1": string }>() {
  type M = { [P in keyof T as P extends string ? P : never]: T[P] };
  let k1: keyof M = "1";
}
const sentinel: number = "";
