// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const arr: number[];
declare const o: { y: boolean; [k: string]: boolean };
function g() { return q; }
const k = g() ? "a" : "b";
o[k] ? () => { const [,] = arr; } : 0;
const q = o.y;
const sentinel: number = "";
