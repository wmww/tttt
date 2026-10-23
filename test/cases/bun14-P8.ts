// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f<K extends "rw">(o: { readonly ro: number; rw: number }, k: K | "ro") {
  o[k] = 1;
}
const sentinel: number = "";
