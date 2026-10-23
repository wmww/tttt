// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type T = { readonly a: unique symbol; readonly b: unique symbol };
let n: number = null! as T["a"];
let p: T["a"] = null! as T["b"];
const sentinel: number = "";
