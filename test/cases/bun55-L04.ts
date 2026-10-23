// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowUnreachableCode": false } }
// @filename: a.ts
declare function fail(): never;
function f(o: any): number { with (fail()) { } }
function g(o: any) { with (fail()) { } let x: string | number = 1; x.length; }
const sentinel: number = "";
