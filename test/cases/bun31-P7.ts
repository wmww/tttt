// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const o: { x: { y: boolean }; [k: string]: any };
function g() { return q; }
const k = g() ? "a" : "b";
o[k] = true;
const q = (o.x = { y: true }).y;
const sentinel: number = "";
