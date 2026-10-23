// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class B { static x: number; y!: string; }
type TB = typeof B;
class D1 implements TB {}
const sentinel: number = "";
