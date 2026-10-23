// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const sy: unique symbol;
function f(u: { [sy]: "a"; a: string } | { [sy]: "b" }) { const { [sy]: k } = u; if (k === "a") u.a; }
const sentinel: number = "";
