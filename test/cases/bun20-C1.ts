// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function assert(x: unknown): asserts x;
function t1(x?: { f(): boolean }) { assert(x?.f()); x.f(); }
const sentinel: number = "";
