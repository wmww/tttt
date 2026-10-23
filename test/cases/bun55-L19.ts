// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare let n: null;
function f1({ range: [lo, hi] = n || [0, 1] }) {}
f1({ range: [1, 2, 3] });
function f2([[a, b] = n ?? [1, "x"]]) {}
f2([["x", 1]]);
let z = f2;
z = true;
// without `??` the default is a tuple in both
function f3([[a, b] = [1, "x"]]) {}
f3([["x", 1]]);
const sentinel: number = "";
