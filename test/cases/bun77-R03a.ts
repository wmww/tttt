// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const u: Unresolved<number>;
function* g1() { const r = yield* u; const t: never = r; }
async function* g2() { const r = yield* u; const t: never = r; }
function* g3(): Generator<number> { const r = yield* u; const t: never = r; }
function* g4() { for (const x of u) { const t: never = x; } }
const sentinel: number = "";
