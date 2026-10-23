// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function cond(): boolean;
function q1() { let x: string | number = ""; while (cond()) { try { if (cond()) { x = 1; throw 0; } } finally { if (cond()) continue; } x.length; } }
const sentinel: number = "";
