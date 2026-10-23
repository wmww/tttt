// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es5"], "target": "es2022" } }
// @filename: a.ts
declare const i1: { a: 1 };
function* g1() { yield* i1; }
function* g2() { yield* [1]; }
function* g3() { yield* ""; }
async function* g4() { yield* i1; }
const sentinel: number = "";
