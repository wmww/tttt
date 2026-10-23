// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const key = compute({ a: 1 }, {}, "a");
function compute(o: { a: number }, p: any, i: "a") { p[key] = 1; return o[i]; }
const key2 = compute2({ a: 1 }, {}, "a");
function compute2(o: { a: number }, p: any, i: "a") { if (p[key2]) {} return o[i]; }
const key3 = compute3({ a: 1 }, {});
function compute3(o: { a: number }, p: any) { let i: "a" = "a"; p[key3] = 1; return o[i]; }
// a key with a name: TS7023 in both
const key4 = compute4({ a: 1 }, {});
function compute4(o: { a: number }, p: any) { const i = "a"; p[key4] = 1; return o[i]; }
const sentinel: number = "";
