// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class A { c() {} }
function mix<T extends new (...a: any[]) => A>(B: T) { return class M extends B {}; }
class D extends mix(A) { get c() { return () => {} } }
const sentinel: number = "";
