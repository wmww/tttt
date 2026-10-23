// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare let c: boolean; declare function fail(): never; declare function assertS(x: unknown): asserts x is string;
declare function get(): (x: unknown) => asserts x is string;
function g1(x: unknown) { for (assertS(x); c;) { x.length; } }
function g2(): number { for (fail(); c;) { } }
function g3() { let y: number; for (fail(); c;) { } y.toFixed(); }
function g4(x: unknown) { for (get()(x); c;) { } }
const sentinel: number = "";
