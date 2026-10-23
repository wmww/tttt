// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface TL extends Array<string | number> { 0: string }
declare const tl: TL;
let a: string | number;
[a] = tl;
a.toUpperCase();
const sentinel: number = "";
