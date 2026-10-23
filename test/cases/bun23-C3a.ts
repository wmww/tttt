// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface A { readonly w: unique symbol }
interface B { readonly w: unique symbol }
declare let x: A["w"]; const y: B["w"] = x;
const sentinel: number = "";
