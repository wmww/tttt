// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type M = { [K in "x" | "p"]: K extends "p" ? typeof arr : number };
interface C { x: number; p: any }
declare let m: M; declare let c: C;
const arr = [m, c];
const sentinel: number = "";
