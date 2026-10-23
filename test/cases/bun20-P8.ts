// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type U = { kind: "a"; a: number } | { kind: "b"; b: string };
declare function assertA(k: string): asserts k is "a";
function f(x: U) { assertA((x.kind)); x.a; }
const sentinel: number = "";
