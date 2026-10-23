// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class N { static a = 1; }
const k = compute();
function check(p: unknown) { return k; }
function compute() { if (check(N)) { } return N.a; }
const sentinel: number = "";
