// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
abstract class A { get x(): number { return 1 } abstract set x(v: number); }
class D extends A {}
const sentinel: number = "";
