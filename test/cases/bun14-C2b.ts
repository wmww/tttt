// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f<T extends Uppercase<string> | number>(t: T) {
  const n: boolean = t;
}
const sentinel: number = "";
