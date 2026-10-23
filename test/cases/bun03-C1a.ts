// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
abstract class A { abstract m(): void; }
function mix<T extends abstract new (...a: any[]) => A>(B: T) { abstract class M extends B {} return M; }
class D extends mix(A) {}
const sentinel: number = "";
