// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
abstract class A1 { abstract x: number; constructor() { let z; ({ x: z } = (this)); } }
const sentinel: number = "";
