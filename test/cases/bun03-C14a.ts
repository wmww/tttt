// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
abstract class A { foo(): void; abstract foo(x: number): void; }
class D extends A {}
const sentinel: number = "";
