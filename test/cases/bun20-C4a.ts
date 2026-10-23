// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function cond(): boolean;
function t8b() { if (cond()) { var a = []; } a.push(1); return a; }
const sentinel: number = "";
