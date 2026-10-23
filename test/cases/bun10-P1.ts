// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const a = [{ q: { u1: 1, r: { ...kk } } }, { q: { u2: 1, get r() { return a; } } }];
declare const kk: K;
class K { v = a; }
const sentinel: number = "";
