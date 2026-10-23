// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type U = { kind: "a"; a: number } | { kind: "b"; b: string };
interface O { v: U; isA(k: string): k is "a"; }
function g(o: O) { if (o.isA(o.v.kind)) { o.v.a; } }
const sentinel: number = "";
