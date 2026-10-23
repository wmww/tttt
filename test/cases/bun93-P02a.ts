// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "target": "es2022" } }
// @filename: a.ts
declare const dec: any, m: any;
function f() { m(@dec x) }
const sentinel: number = "";
