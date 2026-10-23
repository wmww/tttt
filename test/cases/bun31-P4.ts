// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const o: { y: boolean };
function f(p: unknown) { return q; }
f(o);
const q = o.y;
const sentinel: number = "";
