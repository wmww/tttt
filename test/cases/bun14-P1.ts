// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f<T extends unknown, U extends T>() {
  const x: U = 1;
}
const sentinel: number = "";
