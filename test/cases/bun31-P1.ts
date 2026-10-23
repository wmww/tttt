// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f(): { x: { y: boolean } };
declare const o: { y: boolean; [k: string]: boolean };
function g() { return q; }
const k = g() ? "a" : "b";
o[k] = true;
const q = (f().x = { y: true }).y;
const sentinel: number = "";
