// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const o: { y: boolean };
declare const a: Record<string, number>;
function g() { return q; }
const k = g() ? "a" : "b";
a[k] = 1;
const q = o.y;
const sentinel: number = "";
