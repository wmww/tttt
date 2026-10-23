// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f(): { y: boolean };
declare const o: { [k: string]: boolean };
function g() { return q; }
const k = g() ? "a" : "b";
o[k] = true;
const q = f()["y"];
const sentinel: number = "";
