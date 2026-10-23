// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f(x: [number] | string) {
  if (x instanceof Array) return x[0];
  return x.toUpperCase();
}
const sentinel: number = "";
