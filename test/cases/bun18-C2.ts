// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f<V extends 1 | 2>(v: V & 1, w: { a?: 1 }) {
  if (v === w) {}
  w as V & 1;
}
const sentinel: number = "";
