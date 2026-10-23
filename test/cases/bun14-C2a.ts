// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f<T extends Uppercase<string>>(y: T & `A${string}`) {
  const n: number = y;
}
const sentinel: number = "";
