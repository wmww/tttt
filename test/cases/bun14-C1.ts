// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface Box<T> { v: T }
function f<T extends Box<T["v"]>>(t: T) {
  const z: boolean = t.v;
}
const sentinel: number = "";
