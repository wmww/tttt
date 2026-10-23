// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const Ctor: new () => [number, string];
function f(x: [string, number] | Date) {
  if (x instanceof Ctor) return x[0];
  return x.getTime();
}
const sentinel: number = "";
