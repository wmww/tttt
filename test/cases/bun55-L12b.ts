// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
namespace NS { export let a = 1; }
const k1 = c1({});
function c1(p: any) { if (p?.[k1].b) {} return (NS satisfies {})["a"]; }
// without `satisfies`: TS7023 in both
const k2 = c2({});
function c2(p: any) { if (p?.[k2].b) {} return (NS)["a"]; }
const sentinel: number = "";
