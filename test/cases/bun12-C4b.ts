// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function i<T extends [1, 2]>() {
  type M = { [P in keyof T as P extends `${number}` ? P : never]: T[P] };
  let k1: keyof M = "0";
}
const sentinel: number = "";
