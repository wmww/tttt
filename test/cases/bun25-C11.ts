// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
abstract class A { abstract p: number; constructor() { let p; ({ p } = (this)); } }
const sentinel: number = "";
