// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type TL = { readonly u: unique symbol };
declare let tl: TL;
let s13: string = tl.u;
const sentinel: number = "";
