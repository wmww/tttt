// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const k10: I10[typeof k10b];
declare const k10b: "w";
interface I10 { [k10]: 2; [k10b]: "v"; }
declare const i: I10;
const p: boolean = i.w;
type T = I10["w"];
type K = keyof I10;
const kk: K = "w";
const lit: I10 = { w: "v" };
const sentinel: number = "";
