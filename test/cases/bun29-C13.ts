// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare class G { static s: number }
declare function g(): typeof G;
let n: number = function G() { return g(); };
const sentinel: number = "";
