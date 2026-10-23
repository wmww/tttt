// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
enum N { z = -0 }
const b: N = 0;
const sentinel: number = "";
