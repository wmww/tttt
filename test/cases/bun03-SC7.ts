// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "strictPropertyInitialization": false } }
// @filename: a.ts
class B { x: number = 1; }
class D extends B { x: number; }
const sentinel: number = "";
// @filename: b.ts
interface D { x: number }
