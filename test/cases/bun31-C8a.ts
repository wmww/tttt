// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function dec(...x: any[]): any;
function m() { let y: string | number = 1; class K { @dec(y = "a") p: any; } y.toFixed(); }
function n() { let y: number; class K { @dec(y = 1) p: any; } y; }
const sentinel: number = "";
