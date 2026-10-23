// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const k10: I10[typeof k10b];
declare const k10b: "w";
interface I10 { [k10b]: "v"; [k10]: 2 }
const sentinel: number = "";
