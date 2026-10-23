// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function dec(...x: any[]): any;
function o() { let y: string | number = 1; class K { static { y = "a"; } @dec(y.toFixed()) p: any; } }
const sentinel: number = "";
