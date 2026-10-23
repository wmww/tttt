// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] }, "files": ["a.ts", "b.ts"] }
// @filename: a.ts
class B { foo = () => {}; }
class D extends B {}
const sentinel: number = "";
// @filename: b.ts
interface D { foo(): void }
