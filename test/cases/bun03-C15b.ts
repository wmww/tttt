// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext" } }
// @filename: a.ts
import a from "./missing" with { type: 1 };
undeclared;
const sentinel: number = "";
