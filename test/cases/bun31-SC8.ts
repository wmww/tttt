// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowUnreachableCode": false } }
// @filename: a.ts
declare function use(...a: any[]): void; declare function fail(): never; declare function assertS(x: unknown): asserts x is string;
function w1(x: any) { with (fail()) { } use(); }
function w2(x: unknown) { with (assertS(x)) { } const n: never = x; }
declare let o: any;
function w3() { with (o) let a = 1; a; }
function w5() { let a = 1; with (o) let a = 2; a; }
const sentinel: number = "";
