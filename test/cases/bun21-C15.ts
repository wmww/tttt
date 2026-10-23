// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function fail(): never;
function f(o: any): number { with (o) { fail(); } }
const sentinel: number = "";
