// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f(): void;
class C1 { [f(), "a"]() {} }
class C2 { get [f(), "b"]() { return 1 } }
interface I1 { get [f(), "c"](): number }
const sentinel: number = "";
