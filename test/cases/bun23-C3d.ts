// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare let x: import("./m1").A["w"]; const y: import("./m1").B["w"] = x;
const sentinel: number = "";
// @filename: m1.ts
export interface A { readonly w: unique symbol }
export interface B { readonly w: unique symbol }
