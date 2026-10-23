// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function* g5() { const f = (a = yield "x") => { }; }
function* g6() { const f = (a = yield) => { }; }
const sentinel: number = "";
