// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const sy: unique symbol;
let p1: (x: any) => x is typeof sy = null! as (x: any) => x is string;
const sentinel: number = "";
