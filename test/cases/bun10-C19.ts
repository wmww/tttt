// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class A5 { get x() { return 1; } set x(this: { a: 1 }) { } }
const sentinel: number = "";
