// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f<T extends { "1": string }>() {
  type N = { [P in keyof T as P extends number ? P : never]: T[P] };
  let n1: keyof N = 1;
}
const sentinel: number = "";
