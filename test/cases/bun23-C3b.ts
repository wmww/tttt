// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const s: unique symbol;
type TL = { readonly w: unique symbol };
declare let x: TL["w"]; const y: typeof s = x;
const sentinel: number = "";
